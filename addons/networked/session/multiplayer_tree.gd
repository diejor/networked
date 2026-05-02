@tool
class_name MultiplayerTree
extends Node

## Core networking node that bridges a [BackendPeer] transport with Godot's
## [SceneMultiplayer] API.
##
## Assign a [BackendPeer] (e.g. [ENetBackend], [WebSocketBackend]), then call
## [method host] or [method join] to start a session. Add a
## [MultiplayerSceneManager] as a child to manage multiple scenes, or drop a
## world scene directly as a child to use a single auto-configured scene.
## [codeblock]
## # Server
## await multiplayer_tree.host()
##
## # Client
## var err = await multiplayer_tree.join("192.168.1.5", "PlayerOne")
## if err != OK:
##     push_error("Join failed: %s" % error_string(err))
## [/codeblock]

## Emitted when the multiplayer API and scene manager have been configured.
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

## The local player node for this tree.
## [br][br]
## [b]Note:[/b] This is [code]null[/code] on dedicated servers or before the
## player has spawned.
var authority_client: Node:
	set(value):
		if authority_client != value:
			authority_client = value
			authority_client_changed.emit(value)

## Emitted when [member authority_client] is assigned or cleared.
signal authority_client_changed(client: Node)

## Emitted after a player's target scene has been activated and the spawner
## has been dispatched. Useful for custom spawn flows that need to react
## after scene readiness is guaranteed.
signal player_scene_ready(
	client_data: MultiplayerClientData, scene: MultiplayerScene
)

## Emitted on every peer when the game is paused via [method NetwTree.pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via [method NetwTree.unpause].
signal tree_unpaused()
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on the server when a client requests to disconnect.
signal disconnect_requested(peer_id: int, reason: String)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)


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


var _peer_contexts: Dictionary[int, NetwPeerContext] = {}
var _services: Dictionary[Script, Node] = {}


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
			"Service %s already registered - overwriting.",
			[type.get_global_name()], func(m): push_warning(m)
		)
	
	_services[type] = service
	Netw.dbg.debug("Service %s registered.", [type.get_global_name()])


## Unregisters a [Node] from this session's services.
func unregister_service(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()
	
	if _services.get(type) == service:
		_services.erase(type)
		Netw.dbg.debug("Service %s unregistered.", [type.get_global_name()])


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get(type)


## Forcefully clears all internal states and services to break circular
## references during teardown.
func dispose() -> void:
	_services.clear()
	_peer_contexts.clear()


## Returns the [NetwPeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = NetwPeerContext.new()
	return _peer_contexts[peer_id]


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var slot := SpawnSlot.new()
	var sm: MultiplayerSceneManager = get_service(MultiplayerSceneManager)
	
	if sm:
		var scene_name := StringName(spawner_path.get_scene_name())
		var scene: MultiplayerScene = sm.active_scenes.get(scene_name)
		if is_instance_valid(scene):
			slot._scene = scene
			if scene.has_meta(&"_net_scene_token"):
				slot.token = scene.get_meta(&"_net_scene_token")
	
	return slot


## Returns an array of all active player nodes across all scenes.
func get_all_players() -> Array[Node]:
	var sm: MultiplayerSceneManager = get_service(MultiplayerSceneManager)
	if sm:
		return sm.get_all_players()
	return []


## Finds the [Scene] node that contains [param node] by walking its ancestor
## chain. Returns [code]null[/code] if [param node] is not inside any [Scene].
static func scene_for_node(node: Node) -> MultiplayerScene:
	var p := node.get_parent()
	while p:
		if p is MultiplayerScene:
			return p as MultiplayerScene
		p = p.get_parent()
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
	
	var has_scene_manager := false
	var has_sceneless_world := false
	for child in get_children():
		if child is MultiplayerSceneManager:
			has_scene_manager = true
			break
		if _has_spawner_component(child):
			has_sceneless_world = true
			break
	
	if not has_scene_manager and not has_sceneless_world:
		warnings.append(
			"No world scene (containing a SpawnerComponent) or " +
			"MultiplayerSceneManager found as a child. " +
			"No replication will happen."
		)
	
	return warnings


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	_connect_backend_signals()
	
	if is_online():
		_config_api()
	
	for child in get_children():
		if child is MultiplayerSceneManager:
			return
	
	for child in get_children():
		if _has_spawner_component(child):
			var scene_path := child.scene_file_path
			if scene_path.is_empty():
				push_error(
					"[networked] World '%s' must be a saved .tscn." % child.name
				)
				return
			Netw.dbg.info(
				"Default scene: using '%s' as the session world.", [child.name]
			)
			remove_child(child)
			child.queue_free()
			var manager := MultiplayerSceneManager.new()
			manager.name = &"SceneManager"
			add_child(manager)
			manager._configure_default(scene_path)
			return


static func _has_spawner_component(node: Node) -> bool:
	if node is SpawnerComponent:
		return true
	for child in node.get_children():
		if _has_spawner_component(child):
			return true
	return false


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
					"Setup failed: %s", [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = backend.host()
	
	if connection_code == OK:
		_config_api()
	elif not quiet:
		Netw.dbg.error(
			"Failed to host: %s", [error_string(connection_code)], 
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
		"MultiplayerTree: Joining at %s with username %s.",
		[server_address, username]
	)
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			if not quiet:
				Netw.dbg.error(
					"Setup failed: %s", [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = backend.join(server_address, username)
	if connection_code != OK:
		if not quiet:
			Netw.dbg.error(
				"Failed to join: %s", [error_string(connection_code)], 
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


## Disconnects the local peer from the session.
##
## Saves all registered [SaveComponent] states for the local peer, then
## closes the multiplayer peer.
func disconnect_peer() -> void:
	var peer_id := multiplayer_api.get_unique_id() if multiplayer_api else 0
	if peer_id != 0:
		SaveComponent._save_all_in(get_peer_context(peer_id))
	if multiplayer_api and multiplayer_api.has_multiplayer_peer():
		multiplayer_api.multiplayer_peer.close()


## Entry point for a client to request entry into the game world.
##
## Deserializes [param bytes] into a [MultiplayerClientData] and emits
## [signal player_join_requested] for the [MultiplayerSceneManager] to handle.
@rpc("any_peer", "call_remote", "reliable")
func request_join_player(bytes: PackedByteArray) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(bytes)
	client_data.peer_id = peer_id
	
	_resolve_username_collision(client_data)
	
	player_join_requested.emit(client_data)


# ---------------------------------------------------------------------------
# RPCs — pause / unpause (hard, SceneTree-level, moved from MultiplayerScene)
# ---------------------------------------------------------------------------

@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_pause(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		return
	get_tree().paused = true
	tree_paused.emit(reason)


@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_unpause() -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		return
	get_tree().paused = false
	tree_unpaused.emit()


# ---------------------------------------------------------------------------
# RPCs — kick (session-level, moved from MultiplayerScene)
# ---------------------------------------------------------------------------

## Sent by the server to a specific peer to inform them they are being kicked.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_kicked(reason: String) -> void:
	kicked.emit(reason)


## Sent by a client to ask the server to kick another peer.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_kick(target_peer_id: int, reason: String) -> void:
	var requester_id := multiplayer.get_remote_sender_id()
	kick_requested.emit(requester_id, target_peer_id, reason)


# ---------------------------------------------------------------------------
# RPCs — disconnect (session-level)
# ---------------------------------------------------------------------------

## Sent by a client to ask the server for permission to disconnect.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_disconnect(reason: String) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	disconnect_requested.emit(peer_id, reason)


## Sent by the server to notify clients it is shutting down.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_receive_notify_disconnect(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		return
	server_disconnecting.emit(reason)


func _resolve_username_collision(client_data: MultiplayerClientData) -> void:
	var existing_names: Array[StringName] = []
	for player in get_all_players():
		var client := SpawnerComponent.unwrap(player)
		if client:
			existing_names.append(client.username)
		else:
			var parsed := player.name.get_slice("|", 0)
			if not parsed.is_empty():
				existing_names.append(StringName(parsed))
	
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
			"Debug name collision: renaming %s to %s",
			[original_name, new_name]
		)
		client_data.username = new_name
	else:
		Netw.dbg.warn(
			"Username collision detected for '%s'. "
			+ "Topology nameplates may break.", [original_name],
			func(m): push_warning(m)
		)


func _config_api() -> void:
	Netw.dbg.trace("MultiplayerTree: Configuring multiplayer API.")
	
	_tree_name = name
	
	var multiplayer_root := get_path()
	Netw.dbg.debug(
		"Configuring multiplayer API with root: %s", [multiplayer_root]
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


func _connect_backend_signals() -> void:
	if not multiplayer_api:
		return
	if not multiplayer_api.peer_connected.is_connected(_on_peer_connected):
		multiplayer_api.peer_connected.connect(_on_peer_connected)
	if not multiplayer_api.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer_api.peer_disconnected.connect(_on_peer_disconnected)
	if not multiplayer_api.connected_to_server.is_connected(_on_connected_to_server):
		multiplayer_api.connected_to_server.connect(_on_connected_to_server)
	if not multiplayer_api.server_disconnected.is_connected(_on_server_disconnected):
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
	
	_disconnect_backend_signals()
	
	# When re-parenting, we only unregister the API from the previous path 
	# to keep the connection alive. [method _enter_tree] handles re-registration.
	if not is_queued_for_deletion():
		if backend:
			backend.unregister_tree(get_tree())
		return
	
	var debugger = null
	if Engine.has_singleton("NetworkedDebugger"):
		debugger = Engine.get_singleton("NetworkedDebugger")
	elif get_tree().root.has_node("NetworkedDebugger"):
		debugger = get_tree().root.get_node("NetworkedDebugger")
	
	if debugger:
		debugger.unregister_tree(self)
	
	if multiplayer_api and multiplayer_api.has_meta(&"_multiplayer_tree"):
		multiplayer_api.remove_meta(&"_multiplayer_tree")
	
	if backend:
		backend.unconfigure_tree(get_tree())
	
	dispose()


func _on_peer_connected(peer_id: int) -> void:
	Netw.dbg.info("Peer connected: %d", [peer_id])
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	Netw.dbg.info("Peer disconnected: %d", [peer_id])
	_peer_contexts.erase(peer_id)
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d.", [peer_id])
	
	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	Netw.dbg.info("Disconnected from server.")
	server_disconnected.emit()
