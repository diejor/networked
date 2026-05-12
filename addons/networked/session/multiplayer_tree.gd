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

## Emitted on every peer after the server accepts a player join.
signal player_joined(join_payload: JoinPayload)

## Emitted when this peer's player join has been accepted by the server.
signal local_player_joined(join_payload: JoinPayload)

## Emitted when an external invitation is received (e.g. Steam Join Requested).
## [br][br]
## [b]Steam Context:[/b]
## [br]- [param address]: The 64-bit Steam Lobby ID as a [String].
## [br]- [param sender]: The 64-bit Steam ID of the inviting friend.
signal invite_received(address: String, sender: int)


## Emitted after the host's startup scenes have been spawned and the server
## is ready to accept the local player. Only relevant for listen-server hosts.
signal host_ready()

## Emitted when the connection state changes.
signal state_changed(old_state: State, new_state: State)


enum State { OFFLINE, CONNECTING, ONLINE, DISCONNECTING }
enum Role { NONE, CLIENT, DEDICATED_SERVER, LISTEN_SERVER }

## The current connection state of this tree.
var state: State = State.OFFLINE:
	set(new_state):
		if state == new_state:
			return
		var old := state
		state = new_state
		state_changed.emit(old, new_state)

## The current role of this tree in the session.
var role: Role = Role.NONE

## Returns [code]true[/code] while this tree is acting as a server
## (dedicated or listen-server).
var is_host: bool:
	get:
		_warn_if_role_unset()
		return role == Role.DEDICATED_SERVER or role == Role.LISTEN_SERVER

## Returns [code]true[/code] while this tree is acting as a local client
## (including listen-server hosts, which are also their own client).
var is_local_client: bool:
	get:
		_warn_if_role_unset()
		return role == Role.CLIENT or role == Role.LISTEN_SERVER

## Backward compat. Maps to [member is_host].
var is_server: bool:
	get: return is_host
	set(value):
		if value:
			role = Role.DEDICATED_SERVER


func _warn_if_role_unset() -> void:
	if role == Role.NONE:
		Netw.dbg.warn(
			"Accessed role-dependent property before role is set. "
			+ "Connect to 'configured' before reading is_host/is_local_client."
		)

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

## When set, [method connect_player] is called automatically on
## [code]_ready[/code].
@export var init_join_payload: JoinPayload

## On headless builds, automatically calls [method host] on
## [code]_ready[/code].
@export var auto_host_headless: bool = true

## [b]Deprecated.[/b] Temporary opt-in for true listen-server mode.
## When [code]true[/code], localhost connections host directly on this
## tree instead of duplicating into a sibling server node.
## TODO: Remove once listen-server is fully validated and becomes the
## default behavior.
@export var use_listen_server: bool = false:
	set(value):
		use_listen_server = value
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
var local_player: Node:
	set(value):
		if local_player != value:
			local_player = value
			local_player_changed.emit(value)

## Emitted when [member local_player] is assigned or cleared.
signal local_player_changed(player: Node)

## Emitted after a player's target scene has been activated and the spawner
## has been dispatched. Useful for custom spawn flows that need to react
## after scene readiness is guaranteed.
signal player_scene_ready(
	join_payload: JoinPayload, scene: MultiplayerScene
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
	if node is MultiplayerTree:
		return node
	var api := node.multiplayer as SceneMultiplayer
	if not api or not api.has_meta(&"_multiplayer_tree"):
		return null
	return api.get_meta(&"_multiplayer_tree") as MultiplayerTree


## Returns the [member role] of the [MultiplayerTree] associated with [param node].
static func get_role_for(node: Node) -> Role:
	var mt := for_node(node)
	return mt.role if mt else Role.NONE


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
var _joined_players: Dictionary[int, JoinPayload] = {}
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


## Scans descendant nodes for one whose type matches [param type].
## Works in the editor, unlike [method get_service] which only reflects
## nodes that have already called [method register_service].
func find_service_node(type: Script) -> Node:
	var type_name := type.get_global_name()
	if not type_name.is_empty():
		var matches := find_children("*", type_name, true)
		if not matches.is_empty():
			return matches[0]
	else:
		for child in find_children("*", "", true):
			if child.get_script() == type:
				return child
	return null


## Forcefully clears all internal states and services to break circular
## references during teardown.
func dispose() -> void:
	_services.clear()
	_peer_contexts.clear()
	_joined_players.clear()


## Returns the [NetwPeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = NetwPeerContext.new()
	return _peer_contexts[peer_id]


## Returns accepted player join payloads known by this peer.
func get_joined_players() -> Array[JoinPayload]:
	var players: Array[JoinPayload] = []
	for join_payload: JoinPayload in _joined_players.values():
		players.append(_clone_join_payload(join_payload))
	return players


## Returns the accepted player payload for [param peer_id], or
## [code]null[/code].
func get_joined_player(peer_id: int) -> JoinPayload:
	var join_payload := _joined_players.get(peer_id) as JoinPayload
	return _clone_join_payload(join_payload) if join_payload else null


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
	
	if use_listen_server:
		if not find_service_node(ActiveSceneView):
			warnings.append(
				"use_listen_server is enabled but no ActiveSceneView was " +
				"found as a descendant. The listen-server host will not " +
				"be able to see the SubViewport the server-player is " +
				"currently in."
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
	assert(state == State.OFFLINE, "Must be offline to host.")
	Netw.dbg.trace("MultiplayerTree: Hosting session.")
	state = State.CONNECTING
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			state = State.OFFLINE
			if not quiet:
				Netw.dbg.error(
					"Setup failed: %s", [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = await backend.host()
	
	if connection_code == OK:
		role = Role.DEDICATED_SERVER
		state = State.ONLINE
		_config_api()
	else:
		state = State.OFFLINE
		if not quiet:
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
	assert(state == State.OFFLINE, "Must be offline to join.")
	Netw.dbg.trace(
		"MultiplayerTree: Joining at %s with username %s.",
		[server_address, username]
	)
	state = State.CONNECTING
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			state = State.OFFLINE
			if not quiet:
				Netw.dbg.error(
					"Setup failed: %s", [error_string(setup_err)], 
					func(m): push_error(m)
				)
			return setup_err
	
	var connection_code: Error = await backend.join(server_address, username)
	if connection_code != OK:
		state = State.OFFLINE
		if not quiet:
			Netw.dbg.error(
				"Failed to join: %s", [error_string(connection_code)], 
				func(m): push_error(m)
			)
		return connection_code
	
	var timer := get_tree().create_timer(timeout)
	if await Async.timeout(connected_to_server, timer):
		state = State.OFFLINE
		if not quiet:
			Netw.dbg.error("Connection timed out. Server probably is not up, \
consider using `connect_player` instead of `join`.", func(m): push_error(m))
		return ERR_CANT_CONNECT
	
	role = Role.CLIENT
	state = State.ONLINE
	_config_api()
	return OK

## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	return (multiplayer_peer != null 
		and not multiplayer_peer is OfflineMultiplayerPeer 
		and multiplayer_api != null 
		and multiplayer_api.has_multiplayer_peer())


## Saves game state, closes the multiplayer peer, and waits for the server
## to acknowledge disconnection.
func disconnect_player() -> void:
	if state == State.OFFLINE:
		return
	
	Netw.dbg.trace("MultiplayerTree: disconnect_player called.")
	Netw.dbg.info("Disconnecting player.")
	
	state = State.DISCONNECTING
	
	var peer_id := multiplayer_api.get_unique_id() if multiplayer_api else 0
	if peer_id != 0:
		SaveComponent._save_all_in(get_peer_context(peer_id))
	if multiplayer_api and multiplayer_api.has_multiplayer_peer():
		multiplayer_api.multiplayer_peer.close()
	
	var timer := get_tree().create_timer(3.0)
	if multiplayer_api:
		await Async.timeout(multiplayer_api.server_disconnected, timer)
	
	state = State.OFFLINE
	role = Role.NONE
	
	var parent := get_parent()
	if parent:
		var server := parent.get_node_or_null("Server") as MultiplayerTree
		if server and server != self:
			server.queue_free.call_deferred()


## Validates [param join_payload], probes for an existing localhost server,
## then either joins it or spins up an embedded server by duplicating this
## tree into a sibling node.
##
## Returns [code]OK[/code] on success.
func connect_player(join_payload: JoinPayload) -> Error:
	assert(state == State.OFFLINE, "Must be offline to connect.")
	if not join_payload:
		Netw.dbg.error(
			"connect_player: join_payload is null.", func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER
	if join_payload.username.is_empty():
		Netw.dbg.error(
			"connect_player: username is empty.", func(m): push_error(m)
		)
		return ERR_INVALID_PARAMETER
	var has_spawner := (
		join_payload.spawner_component_path
		and join_payload.spawner_component_path.is_valid()
	)
	
	await disconnect_player()
	
	var url := join_payload.url
	Netw.dbg.info(
		"Connecting player %s to %s", [join_payload.username, url]
	)
	
	if _is_local_url(url):
		if backend.supports_embedded_server():
			var probe_url := url if not url.is_empty() else "localhost"
			var probe_err: Error = await join(
				probe_url, join_payload.username, 1.0, true
			)
			if probe_err == OK:
				submit_join(join_payload)
				return OK
			
			if use_listen_server:
				var host_err := await host(true)
				if host_err == OK:
					role = Role.LISTEN_SERVER
					await host_ready
					submit_join(join_payload)
					return OK
				elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
					var join_err := await join(
						backend.get_join_address(), join_payload.username
					)
					if join_err == OK:
						submit_join(join_payload)
					return join_err
				else:
					return host_err
			
			var server := duplicate() as MultiplayerTree
			server.is_server = true
			server.name = "Server"
			server.init_join_payload = null
			server.auto_host_headless = false
			get_parent().add_child.call_deferred(server)
			await get_tree().process_frame
			
			var client_sm := get_service(MultiplayerSceneManager)
			if client_sm:
				var server_sm := server.get_service(MultiplayerSceneManager)
				for path in client_sm._get_configured_paths():
					server_sm._configure_default(path)
			
			var host_err := await server.host(true)
			if host_err == OK:
				var join_err := await join(
					server.backend.get_join_address(), join_payload.username
				)
				if join_err == OK:
					submit_join(join_payload)
				return join_err
			elif host_err == ERR_ALREADY_IN_USE or host_err == ERR_CANT_CREATE:
				server.queue_free.call_deferred()
				var join_err := await join(
					backend.get_join_address(), join_payload.username
				)
				if join_err == OK:
					submit_join(join_payload)
				return join_err
			else:
				server.queue_free.call_deferred()
				return host_err
		else:
			# For backends that don't support embedded servers (like Steam),
			# local URL means we should just host a lobby.
			var host_err := await host(true)
			if host_err == OK:
				role = Role.LISTEN_SERVER
				await host_ready
				submit_join(join_payload)
				return OK
			return host_err
	
	if OS.has_feature("web") and url.begins_with("ws"):
		backend = WebSocketBackend.new()
	
	var err := await join(url, join_payload.username)
	if err == OK:
		submit_join(join_payload)
	return err


## Submits a join request to the server.
func submit_join(join_payload: JoinPayload) -> void:
	request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize()
	)


func _is_local_url(url: String) -> bool:
	return url.is_empty() or "localhost" in url or "127.0.0.1" in url


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	
	if init_join_payload:
		init_join_payload.is_debug = true
		connect_player(init_join_payload)
	
	if auto_host_headless and DisplayServer.get_name() == "headless":
		if use_listen_server:
			await host()
			role = Role.LISTEN_SERVER
		elif is_server:
			await host()


## Entry point for a client to request entry into the game world.
##
## Deserializes [param bytes] into a [JoinPayload], resolves server-authority
## fields, and emits [signal player_joined] on every peer.
@rpc("any_peer", "call_local", "reliable")
func request_join_player(bytes: PackedByteArray) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"request_join_player received on non-server peer %d",
			[multiplayer.get_unique_id()]
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()
	
	var join_payload: JoinPayload = JoinPayload.new()
	join_payload.deserialize(bytes)
	join_payload.peer_id = peer_id

	var rj := join_payload.resolve()
	if not rj:
		Netw.dbg.warn(
			"request_join_player: invalid payload from peer %d",
			[peer_id]
		)
		return

	_resolve_username_collision(join_payload)
	
	_remember_joined_player(join_payload)
	_rpc_notify_player_joined.rpc(join_payload.serialize())
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		_rpc_sync_joined_players.rpc_id(peer_id, _serialize_joined_players())


# Emits the accepted join notification on remote peers.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_notify_player_joined(bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_notify_player_joined received from non-server peer %d",
			[sender]
		)
		return
	
	var join_payload: JoinPayload = JoinPayload.new()
	join_payload.deserialize(bytes)
	_remember_joined_player(join_payload)


# Sends all accepted player payloads to a newly joined peer.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_sync_joined_players(payloads: Array[PackedByteArray]) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		Netw.dbg.warn(
			"_rpc_sync_joined_players received from non-server peer %d",
			[sender]
		)
		return
	
	for bytes: PackedByteArray in payloads:
		var join_payload: JoinPayload = JoinPayload.new()
		join_payload.deserialize(bytes)
		_remember_joined_player(join_payload)


# Emits join signals derived from the accepted server-authority payload.
func _emit_player_joined(join_payload: JoinPayload) -> void:
	player_joined.emit(join_payload)
	
	if join_payload.peer_id == multiplayer.get_unique_id():
		local_player_joined.emit(join_payload)


# Stores an accepted player payload and emits it once on this peer.
func _remember_joined_player(join_payload: JoinPayload) -> bool:
	if _joined_players.has(join_payload.peer_id):
		return false
	
	var stored_payload := _clone_join_payload(join_payload)
	_joined_players[join_payload.peer_id] = stored_payload
	_emit_player_joined(_clone_join_payload(stored_payload))
	return true


# Returns a defensive copy of an accepted join payload.
func _clone_join_payload(join_payload: JoinPayload) -> JoinPayload:
	if not join_payload:
		return null
	
	var clone := JoinPayload.new()
	clone.deserialize(join_payload.serialize())
	clone.resolved = join_payload.resolved
	return clone


# Serializes the locally known accepted player roster.
func _serialize_joined_players() -> Array[PackedByteArray]:
	var payloads: Array[PackedByteArray] = []
	for join_payload: JoinPayload in _joined_players.values():
		payloads.append(join_payload.serialize())
	return payloads


# ---------------------------------------------------------------------------
# RPCs - pause / unpause (hard, SceneTree-level, moved from MultiplayerScene)
# ---------------------------------------------------------------------------

@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_pause(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		Netw.dbg.warn("_rpc_receive_pause received from non-server peer %d", [sender])
		return
	get_tree().paused = true
	tree_paused.emit(reason)


@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_unpause() -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender != 0:
		Netw.dbg.warn("_rpc_receive_unpause received from non-server peer %d", [sender])
		return
	get_tree().paused = false
	tree_unpaused.emit()


# ---------------------------------------------------------------------------
# RPCs - kick (session-level, moved from MultiplayerScene)
# ---------------------------------------------------------------------------

## Sent by the server to a specific peer to inform them they are being kicked.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_kicked(reason: String) -> void:
	kicked.emit(reason)


## Sent by a client to ask the server to kick another peer.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_kick(target_peer_id: int, reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn("_rpc_request_kick received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var requester_id := multiplayer.get_remote_sender_id()
	kick_requested.emit(requester_id, target_peer_id, reason)


# ---------------------------------------------------------------------------
# RPCs - disconnect (session-level)
# ---------------------------------------------------------------------------

## Sent by a client to ask the server for permission to disconnect.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_disconnect(reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn("_rpc_request_disconnect received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var peer_id := multiplayer.get_remote_sender_id()
	disconnect_requested.emit(peer_id, reason)


## Sent by the server to notify clients it is shutting down.
@rpc("any_peer", "call_local", "reliable")
func _rpc_receive_notify_disconnect(reason: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1:
		Netw.dbg.warn("_rpc_receive_notify_disconnect received from non-server peer %d", [sender])
		return
	server_disconnecting.emit(reason)


func _resolve_username_collision(join_payload: JoinPayload) -> void:
	var existing_names: Array[StringName] = []
	for player in get_all_players():
		var entity := NetwEntity.of(player)
		if entity and not entity.entity_id.is_empty():
			existing_names.append(entity.entity_id)
		else:
			var client := SpawnerComponent.unwrap(player)
			if client:
				existing_names.append(client.entity_id)
			else:
				var parsed := player.name.get_slice("|", 0)
				if not parsed.is_empty():
					existing_names.append(StringName(parsed))
	
	var original_name := join_payload.username
	if not original_name in existing_names:
		return
	
	if join_payload.is_debug:
		var suffix := 1
		var new_name := StringName(str(original_name) + str(suffix))
		while new_name in existing_names:
			suffix += 1
			new_name = StringName(str(original_name) + str(suffix))
		
		Netw.dbg.info(
			"Debug name collision: renaming %s to %s",
			[original_name, new_name]
		)
		join_payload.username = new_name
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
	
	Netw.dbg.register_tree(self)
	
	configured.emit()
	
	var sm := get_service(MultiplayerSceneManager)
	if sm and not sm.startup_scenes_spawned.is_connected(host_ready.emit):
		sm.startup_scenes_spawned.connect(host_ready.emit)


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
	
	Netw.dbg.unregister_tree(self)
	
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
	_joined_players.erase(peer_id)
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	Netw.dbg.info("Connected to server as peer %d.", [peer_id])
	
	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	Netw.dbg.info("Disconnected from server.")
	server_disconnected.emit()
