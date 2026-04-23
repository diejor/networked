@tool
class_name MultiplayerTree
extends Node

## Core networking node that bridges a [BackendPeer] transport with Godot's
## [SceneMultiplayer] API.
##
## Assign a [BackendPeer] (e.g. [ENetBackend], [WebSocketBackend]) and a
## [MultiplayerLobbyManager], then call [method host] or [method join] to start
## a session.
## [codeblock]
## # Server
## await multiplayer_tree.host()
##
## # Client
## var err = await multiplayer_tree.join("192.168.1.5", "PlayerOne")
## if err != OK:
##     push_error("Join failed: %s" % error_string(err))
## [/codeblock]

## Emitted when the multiplayer API and lobby manager have been configured.
signal configured()

## Emitted when a new peer connects to the server.
signal peer_connected(peer_id: int)

## Emitted when a peer disconnects from the server.
signal peer_disconnected(peer_id: int)

## Emitted on the client when it successfully connects to the server.
signal connected_to_server()

## Emitted on the client when the server disconnects or crashes.
signal server_disconnected()

## Emitted on the server when a peer requests to join.
signal player_join_requested(client_data: MultiplayerClientData)


## Set to [code]true[/code] to configure this instance as the server.
var is_server: bool

## The transport implementation used for this session.
##
## Example: [ENetBackend], [WebSocketBackend], [WebRTCBackend].
## The resource is automatically duplicated at runtime to ensure isolation.
@export var backend: BackendPeer:
	set(value):
		if not Engine.is_editor_hint():
			if backend:
				_disconnect_backend_signals()
			
			if value:
				backend = value.duplicate()
				backend._copy_from(value)
				_connect_backend_signals()
			else:
				backend = null
		else:
			if backend and backend.changed.is_connected(
				update_configuration_warnings
			):
				backend.changed.disconnect(update_configuration_warnings)
				
			backend = value
			
			if backend and not backend.changed.is_connected(
				update_configuration_warnings
			):
				backend.changed.connect(update_configuration_warnings)
				
		update_configuration_warnings()

## The active [SceneMultiplayer] instance provided by the current
## [member backend].
var multiplayer_api: SceneMultiplayer:
	get:
		return backend.api if backend else null

## The active [MultiplayerPeer] connection managed by the [member backend].
var multiplayer_peer: MultiplayerPeer:
	get:
		if backend and backend.api:
			return backend.api.multiplayer_peer
		return null

var _tree_name: String = ""

## The [ClientComponent] representing the local player identity for this tree.
## [br][br]
## [b]Note:[/b] This is [code]null[/code] on dedicated servers or before the
## player has spawned.
var authority_client: ClientComponent:
	set(value):
		if authority_client != value:
			authority_client = value
			authority_client_changed.emit(value)

## Emitted when [member authority_client] is assigned or cleared.
signal authority_client_changed(client: ClientComponent)


## Returns the original name of the tree, even if renamed for embedded use.
func get_tree_name() -> String:
	return _tree_name if not _tree_name.is_empty() else name


## Locates the [MultiplayerTree] registered on the node's [SceneMultiplayer].
static func for_node(node: Node) -> MultiplayerTree:
	var api := node.multiplayer as SceneMultiplayer
	if not api or not api.has_meta(&"_multiplayer_tree"):
		return null
	return api.get_meta(&"_multiplayer_tree") as MultiplayerTree


## Global resolver that finds a [MultiplayerTree] from any context.
##
## Handles [MultiplayerTree] instances, [Node]s (via metadata or hierarchy),
## and returns [code]null[/code] for invalid contexts.
static func resolve(context: Object) -> MultiplayerTree:
	if context is MultiplayerTree:
		return context
	
	if context is Node:
		var node := context as Node
		var mt := for_node(node)
		if mt:
			return mt
		
		var p := node.get_parent()
		while p:
			if p is MultiplayerTree:
				return p
			p = p.get_parent()
	
	return null


var _peer_contexts: Dictionary[int, PeerContext] = {}
var _services: Dictionary[Script, Node] = {}

var _pending_world: Node = null
var _pending_world_scene_path: String = ""


## Registers a [Node] as a service for this session.
func register_service(service: Node, type: Script = null) -> void:
	assert(
		is_ancestor_of(service) or service == self, 
		"Service %s must be a descendant of the MultiplayerTree." % service.name
	)
	
	if not type:
		type = service.get_script()
	
	if type in _services:
		Netw.dbg.warn(
			"Service %s already registered — overwriting." % \
			[type.get_global_name()], 
			func(m): push_warning(m)
		)
	
	_services[type] = service
	Netw.dbg.debug("Service %s registered." % [type.get_global_name()])


## Unregisters a [Node] from this session's services.
func unregister_service(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()
	
	if _services.get(type) == service:
		_services.erase(type)
		Netw.dbg.debug("Service %s unregistered." % [type.get_global_name()])


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get(type)


## Returns the [PeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> PeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = PeerContext.new()
	return _peer_contexts[peer_id]


## Carries both the causal token and the placement target for a player spawn.
##
## Obtained via [method MultiplayerTree.get_spawn_context].
class SpawnContext extends RefCounted:
	## Causal [CheckpointToken] for span tracing. May be [code]null[/code].
	var token: CheckpointToken
	var _lobby: Lobby
	var _parent_node: Node

	func is_valid() -> bool:
		return is_instance_valid(_lobby) or is_instance_valid(_parent_node)

	func has_lobby() -> bool:
		return is_instance_valid(_lobby)

	## Lobby mode: calls [method Lobby.add_player].
	## [br]
	## Lobbyless mode: calls [method Node.add_child] on the parent container.
	func place_player(player: Node) -> void:
		if is_instance_valid(_lobby):
			_lobby.add_player(player)
		elif is_instance_valid(_parent_node):
			_parent_node.add_child(player)


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_context(spawner_path: SceneNodePath) -> SpawnContext:
	var ctx := SpawnContext.new()
	var lm: MultiplayerLobbyManager = get_service(MultiplayerLobbyManager)

	if lm:
		var scene_name := StringName(spawner_path.get_scene_name())
		var lobby: Lobby = lm.active_lobbies.get(scene_name)
		if is_instance_valid(lobby):
			ctx._lobby = lobby
			if lobby.has_meta(&"_net_lobby_token"):
				ctx.token = lobby.get_meta(&"_net_lobby_token")
	else:
		var world := _find_world(spawner_path)
		if is_instance_valid(world):
			ctx._parent_node = world
			
			var wrapper := world.get_parent()
			if is_instance_valid(wrapper) and \
					wrapper.get_meta(&"_is_world_wrapper", false):
				if wrapper.has_meta(&"_net_session_token"):
					ctx.token = wrapper.get_meta(&"_net_session_token")

	return ctx


## Returns an array of all active player nodes across all lobbies.
func get_all_players() -> Array[Node]:
	var lm: MultiplayerLobbyManager = get_service(MultiplayerLobbyManager)
	if lm:
		return lm.get_all_players()
	
	var players: Array[Node] = []
	for c in find_children("*", "ClientComponent", true, false):
		if is_instance_valid(c.owner):
			players.append(c.owner)
	return players


## Returns the session-level causal token for lobbyless mode.
func get_lobbyless_session_token() -> CheckpointToken:
	for child in get_children():
		if child.get_meta(&"_is_world_wrapper", false):
			if child.has_meta(&"_net_session_token"):
				return child.get_meta(&"_net_session_token")
	return null


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	
	if not backend:
		warnings.append(
			"A BackendPeer resource must be assigned to the 'backend' property."
		)
	elif backend.get_script() != null and \
			backend.get_script().get_global_name() == "BackendPeer":
		warnings.append(
			"The assigned backend is the abstract 'BackendPeer' class. " + \
			"Please assign a functional derived class."
		)
	elif backend:
		warnings.append_array(backend._get_backend_warnings(self))
		
	var has_lobby_manager := false
	var has_lobbyless_world := false
	for child in get_children():
		if child is MultiplayerLobbyManager:
			has_lobby_manager = true
			break
		if _has_client_component(child):
			has_lobbyless_world = true
			break
			
	if not has_lobby_manager and not has_lobbyless_world:
		warnings.append(
			"No Scene (with a ClientComponent inside) or " + \
			"`MultiplayerLobbyManager` found in children. " + \
			"No replication will happen."
		)
		
	return warnings


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	for child in get_children():
		if child is MultiplayerLobbyManager:
			return
	
	for child in get_children():
		if _has_client_component(child):
			Netw.dbg.info(
				"Lobbyless mode: Identified '%s' as the initial world." % \
				[child.name]
			)
			_pending_world = child
			_pending_world_scene_path = child.scene_file_path
			if _pending_world_scene_path.is_empty():
				push_error(
					"[networked] Lobbyless world '%s' must be a saved .tscn." % \
					child.name
				)
				_pending_world = null
				return
			remove_child(child)
			return


static func _has_client_component(node: Node) -> bool:
	if node is ClientComponent:
		return true
	for child in node.get_children():
		if _has_client_component(child):
			return true
	return false


func _create_world_wrapper() -> Node:
	var wrapper: Node
	if is_server:
		var vp := SubViewport.new()
		vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
		wrapper = vp
	else:
		wrapper = Node.new()
	wrapper.name = "World"
	wrapper.set_meta(&"_is_world_wrapper", true)
	return wrapper


func _setup_pending_world() -> void:
	if not _pending_world and _pending_world_scene_path.is_empty():
		return
	
	var span: NetSpan = null
	if not _pending_world_scene_path.is_empty():
		span = Netw.dbg.span(
			self, "session", {"world": _pending_world_scene_path}
		)
		if span:
			span.step("initializing_world")

	var wrapper := _create_world_wrapper()
	if is_server and wrapper is SubViewport:
		wrapper.set("multiplayer", multiplayer_api)
	
	if span:
		wrapper.set_meta(&"_net_session_token", span.checkpoint())
	
	add_child(wrapper)
	if is_server:
		if _pending_world:
			_pending_world.free()
		var level: Node = load(_pending_world_scene_path).instantiate()
		wrapper.add_child(level)
	else:
		wrapper.add_child(_pending_world)
	_pending_world = null
	_pending_world_scene_path = ""
	
	if span:
		span.end()


func _find_world(spawner_path: SceneNodePath) -> Node:
	for child in get_children():
		if not child.get_meta(&"_is_world_wrapper", false):
			continue
		for level in child.get_children():
			if _scene_paths_match(level.scene_file_path, spawner_path.scene_path):
				return level
	return null


static func _scene_paths_match(a: String, b: String) -> bool:
	return SceneNodePath._safe_resolve_path(a) == \
		SceneNodePath._safe_resolve_path(b)


func _route_lobbyless_join(client_data: MultiplayerClientData) -> void:
	var world := _find_world(client_data.spawner_path)
	if not world:
		var scene_path := SceneNodePath._safe_resolve_path(
			client_data.spawner_path.scene_path
		)
		Netw.dbg.error(
			"Lobbyless: no world matches spawner scene '%s'." % scene_path,
			func(m): push_error(m)
		)
		return
	
	var client_comp := world.get_node_or_null(
		client_data.spawner_path.node_path
	) as ClientComponent
	if not client_comp:
		Netw.dbg.error(
			"Lobbyless: ClientComponent not found at '%s'." % \
			client_data.spawner_path.node_path,
			func(m): push_error(m)
		)
		return
	client_comp.player_joined.emit(client_data)


func _init() -> void:
	if not Engine.is_editor_hint():
		tree_exiting.connect(_on_exiting)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return
		
	if backend:
		backend.poll(dt)


## Starts this instance as a network host.
##
## Calls [code]setup()[/code] on the backend if available, then [code]host()[/code].
## Returns [code]OK[/code] on success or a non-zero [enum Error] code on failure.
func host(quiet: bool = false) -> Error:
	Netw.dbg.trace("MultiplayerTree: Hosting session.")
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			if not quiet:
				Netw.dbg.error(
					"Setup failed: %s" % [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = backend.host()
	
	if connection_code == OK:
		_config_api()
	elif not quiet:
		Netw.dbg.error(
			"Failed to host: %s" % [error_string(connection_code)], 
			func(m): push_error(m)
		)
		
	return connection_code


## Connects to an active server at [param server_address].
##
## Awaits [signal connected_to_server] with the specified [param timeout].
## Returns [code]ERR_CANT_CONNECT[/code] if no response arrives in time.
func join(
	server_address: String,
	username: String,
	timeout: float = 5.0,
	quiet: bool = false
) -> Error:
	Netw.dbg.trace(
		"MultiplayerTree: Joining at %s with username %s." % \
		[server_address, username]
	)
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			if not quiet:
				Netw.dbg.error(
					"Setup failed: %s" % [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = backend.join(server_address, username)
	if connection_code != OK:
		if not quiet:
			Netw.dbg.error(
				"Failed to join: %s" % [error_string(connection_code)], 
				func(m): push_error(m)
			)
		return connection_code
	
	var timer := get_tree().create_timer(timeout)
	if await Async.timeout(connected_to_server, timer):
		if not quiet:
			Netw.dbg.error("Connection timed out.", func(m): push_error(m))
		return ERR_CANT_CONNECT
	
	_config_api()
	return OK


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	return (multiplayer_peer != null 
		and not multiplayer_peer is OfflineMultiplayerPeer 
		and multiplayer_api != null 
		and multiplayer_api.has_multiplayer_peer())


## Entry point for a client to request entry into the game world.
##
## Deserializes [param bytes] into a [MultiplayerClientData]. Delegates to 
## [MultiplayerLobbyManager] if registered, otherwise routes as lobbyless.
@rpc("any_peer", "call_remote", "reliable")
func request_join_player(bytes: PackedByteArray) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(bytes)
	client_data.peer_id = peer_id

	_resolve_username_collision(client_data)

	if get_service(MultiplayerLobbyManager):
		player_join_requested.emit(client_data)
	else:
		_route_lobbyless_join(client_data)


func _resolve_username_collision(client_data: MultiplayerClientData) -> void:
	var existing_names: Array[StringName] = []
	for player in get_all_players():
		var client := ClientComponent.unwrap(player)
		if client:
			existing_names.append(client.username)
	
	var original_name := client_data.username
	if not original_name in existing_names:
		return
		
	if client_data.is_debug:
		var suffix := 1
		var new_name := StringName(str(original_name) + str(suffix))
		while new_name in existing_names:
			suffix += 1
			new_name = StringName(str(original_name) + str(suffix))
		
		Netw.dbg.info(
			"Debug name collision: renaming %s to %s" % [original_name, new_name]
		)
		client_data.username = new_name
	else:
		Netw.dbg.warn(
			"Username collision detected for '%s'. " + \
			"Topology nameplates may break." % original_name, 
			func(m): push_warning(m)
		)


func _config_api() -> void:
	Netw.dbg.trace("MultiplayerTree: Configuring multiplayer API.")
	
	_tree_name = name
	
	var multiplayer_root := get_path()
	Netw.dbg.debug(
		"Configuring multiplayer API with root: %s" % [multiplayer_root]
	)
	backend.configure_tree(get_tree(), multiplayer_root)
	multiplayer_api.set_meta(&"_multiplayer_tree", self)
	
	var debugger = null
	if Engine.has_singleton("NetworkedDebugger"):
		debugger = Engine.get_singleton("NetworkedDebugger")
	elif get_tree().root.has_node("NetworkedDebugger"):
		debugger = get_tree().root.get_node("NetworkedDebugger")
		
	if debugger:
		debugger.register_tree(self)

	configured.emit()
	_setup_pending_world()


func _connect_backend_signals() -> void:
	if not multiplayer_api: 
		return
	multiplayer_api.peer_connected.connect(_on_peer_connected)
	multiplayer_api.peer_disconnected.connect(_on_peer_disconnected)
	multiplayer_api.connected_to_server.connect(_on_connected_to_server)
	multiplayer_api.server_disconnected.connect(_on_server_disconnected)


func _disconnect_backend_signals() -> void:
	if not multiplayer_api: 
		return
	if multiplayer_api.peer_connected.is_connected(_on_peer_connected):
		multiplayer_api.peer_connected.disconnect(_on_peer_connected)
	if multiplayer_api.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer_api.peer_disconnected.disconnect(_on_peer_disconnected)
	if multiplayer_api.connected_to_server.is_connected(_on_connected_to_server):
		multiplayer_api.connected_to_server.disconnect(_on_connected_to_server)
	if multiplayer_api.server_disconnected.is_connected(_on_server_disconnected):
		multiplayer_api.server_disconnected.disconnect(_on_server_disconnected)


func _on_exiting() -> void:
	Netw.dbg.trace("MultiplayerTree: Exiting.")
	
	var debugger = null
	if Engine.has_singleton("NetworkedDebugger"):
		debugger = Engine.get_singleton("NetworkedDebugger")
	elif get_tree().root.has_node("NetworkedDebugger"):
		debugger = get_tree().root.get_node("NetworkedDebugger")
		
	if debugger:
		debugger.unregister_tree(self)

	if multiplayer_api and multiplayer_api.has_meta(&"_multiplayer_tree"):
		multiplayer_api.remove_meta(&"_multiplayer_tree")
	_peer_contexts.clear()
	if backend:
		backend.peer_reset_state()


func _on_peer_connected(peer_id: int) -> void:
	Netw.dbg.info("Peer connected: %d" % [peer_id])
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	Netw.dbg.info("Peer disconnected: %d" % [peer_id])
	_peer_contexts.erase(peer_id)
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d." % [peer_id])

	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	Netw.dbg.info("Disconnected from server.")
	server_disconnected.emit()
