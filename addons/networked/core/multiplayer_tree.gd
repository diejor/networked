@tool
class_name MultiplayerTree
extends Node

## Core networking node that bridges a [BackendPeer] transport with Godot's [SceneMultiplayer] API.
##
## Assign a [BackendPeer] (e.g. [ENetBackend], [WebSocketBackend]) and a [MultiplayerLobbyManager],
## then call [method host] or [method join] to start a session.
## [codeblock]
## # Server
## await multiplayer_tree.host()
##
## # Client
## var err = await multiplayer_tree.join("192.168.1.5", "PlayerOne")
## if err != OK:
##     push_error("Join failed: %s" % error_string(err))
## [/codeblock]

## Emitted when the multiplayer API and lobby manager have been successfully configured.
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

## Set to [code]true[/code] to configure this instance as the authoritative server.
var is_server: bool

## The transport implementation used for this session (e.g. [ENetBackend], [WebSocketBackend], [WebRTCBackend]).
##
## The resource is automatically duplicated at runtime to ensure each session gets an isolated state.
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
			if backend and backend.changed.is_connected(update_configuration_warnings):
				backend.changed.disconnect(update_configuration_warnings)
				
			backend = value
			
			if backend and not backend.changed.is_connected(update_configuration_warnings):
				backend.changed.connect(update_configuration_warnings)
				
		update_configuration_warnings()

## The active [SceneMultiplayer] instance provided by the current [member backend].
var multiplayer_api: SceneMultiplayer:
	get: return backend.api if backend else null

## The active [MultiplayerPeer] connection managed by the current [member backend].
var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer if backend and backend.api else null


## Locates the [MultiplayerTree] registered on the [param node]'s [SceneMultiplayer] instance.
static func for_node(node: Node) -> MultiplayerTree:
	var api := node.multiplayer as SceneMultiplayer
	if not api or not api.has_meta(&"_multiplayer_tree"):
		return null
	return api.get_meta(&"_multiplayer_tree") as MultiplayerTree


## Global resolver that finds a [MultiplayerTree] from any context.
## Handles MultiplayerTree instances, Nodes (via metadata or hierarchy), 
## and returns null for invalid contexts.
static func resolve(context: Object) -> MultiplayerTree:
	if context is MultiplayerTree:
		return context
	
	if context is Node:
		# Fast path: metadata lookup
		var node := context as Node
		var mt := for_node(node)
		if mt: return mt
		
		# Fallback: climb hierarchy
		var p := node.get_parent()
		while p:
			if p is MultiplayerTree: return p
			p = p.get_parent()
	
	return null


var _peer_contexts: Dictionary[int, PeerContext] = {}
var _services: Dictionary[Script, Node] = {}


## Registers a [Node] as a service for this session.
func register_service(service: Node, type: Script = null) -> void:
	assert(is_ancestor_of(service) or service == self, "Service %s must be a descendant of the MultiplayerTree." % service.name)
	
	if not type:
		type = service.get_script()
	
	if type in _services:
		NetLog.warn("Service %s already registered — overwriting." % type.get_global_name())
	
	_services[type] = service
	NetLog.debug("Service %s registered." % type.get_global_name())


## Unregisters a [Node] from this session's services.
func unregister_service(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()
	
	if _services.get(type) == service:
		_services.erase(type)
		NetLog.debug("Service %s unregistered." % type.get_global_name())


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get(type)


## Returns the [PeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> PeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = PeerContext.new()
	return _peer_contexts[peer_id]


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	
	if not backend:
		warnings.append("A BackendPeer resource must be assigned to the 'backend' property.")
	elif backend.get_script() != null and backend.get_script().get_global_name() == "BackendPeer":
		warnings.append("The assigned backend is the abstract 'BackendPeer' class. Please assign a functional derived class.")
	elif backend:
		warnings.append_array(backend._get_backend_warnings(self))
		
	return warnings


func _init() -> void:
	if not Engine.is_editor_hint():
		tree_exiting.connect(_on_exiting)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return
		
	if backend:
		backend.poll(dt)


## Starts this instance as a network host and configures the [SceneMultiplayer] API.
##
## Calls [code]setup()[/code] on the backend if available, then [code]host()[/code].
## Returns [code]OK[/code] on success or a non-zero [enum Error] code on failure.
func host(quiet: bool = false) -> Error:
	NetLog.trace("MultiplayerTree: Hosting session.")
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			if not quiet:
				NetLog.error("Setup failed: %s", [error_string(setup_err)], func(m): push_error(m))
			return setup_err
	
	var connection_code: Error = backend.host()
	
	if connection_code == OK:
		_config_api()
	elif not quiet:
		NetLog.error("Failed to host: %s", [error_string(connection_code)], func(m): push_error(m))
		
	return connection_code


## Connects to an active server at [param server_address] using the given [param username].
##
## Awaits [signal connected_to_server] with the specified [param timeout] (default 5.0s).
## Returns [code]ERR_CANT_CONNECT[/code] if no response arrives in time, or another
## [enum Error] code if the backend rejects the connection immediately.
func join(server_address: String, username: String, timeout: float = 5.0, quiet: bool = false) -> Error:
	NetLog.trace("MultiplayerTree: Joining session at %s with username %s." % [server_address, username])
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			if not quiet:
				NetLog.error("Setup failed: %s", [error_string(setup_err)], func(m): push_error(m))
			return setup_err
	
	var connection_code: Error = backend.join(server_address, username)
	if connection_code != OK:
		if not quiet:
			NetLog.error("Failed to join: %s", [error_string(connection_code)], func(m): push_error(m))
		return connection_code
	
	var timer := get_tree().create_timer(timeout)
	if await Async.timeout(connected_to_server, timer):
		if not quiet:
			NetLog.error("Connection timed out.", [], func(m): push_error(m))
		return ERR_CANT_CONNECT
	
	_config_api()
	return OK


## Returns [code]true[/code] if the multiplayer peer is initialized and in an active connection.
func is_online() -> bool:
	return (multiplayer_peer != null 
		and not multiplayer_peer is OfflineMultiplayerPeer 
		and multiplayer_api != null 
		and multiplayer_api.has_multiplayer_peer())


## Entry point for a client to request entry into the game world.
##
## Deserializes [param bytes] into a [MultiplayerClientData]. If a [MultiplayerLobbyManager]
## service is registered, delegates the join to it. Otherwise, emits [signal player_join_requested]
## for manual handling by the user.
@rpc("any_peer", "call_remote", "reliable")
func request_join_player(bytes: PackedByteArray) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(bytes)
	client_data.peer_id = peer_id

	player_join_requested.emit(client_data)


func _config_api() -> void:
	NetLog.trace("MultiplayerTree: Configuring multiplayer API.")
	
	set_meta(&"_original_name", name)
	
	var multiplayer_root := get_path()
	NetLog.debug("Configuring multiplayer API with root: %s" % multiplayer_root)
	backend.configure_tree(get_tree(), multiplayer_root)
	multiplayer_api.set_meta(&"_multiplayer_tree", self)
	
	if Engine.has_singleton("NetworkedDebugger"):
		Engine.get_singleton("NetworkedDebugger").register_tree(self)
	elif get_tree().root.has_node("NetworkedDebugger"):
		get_tree().root.get_node("NetworkedDebugger").register_tree(self)

	configured.emit()


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
	NetLog.trace("MultiplayerTree: Exiting.")
	
	if Engine.has_singleton("NetworkedDebugger"):
		Engine.get_singleton("NetworkedDebugger").unregister_tree(self)
	elif get_tree().root.has_node("NetworkedDebugger"):
		get_tree().root.get_node("NetworkedDebugger").unregister_tree(self)

	if multiplayer_api and multiplayer_api.has_meta(&"_multiplayer_tree"):
		multiplayer_api.remove_meta(&"_multiplayer_tree")
	_peer_contexts.clear()
	if backend:
		backend.peer_reset_state()


func _on_peer_connected(peer_id: int) -> void:
	NetLog.info("Peer connected: %d" % peer_id)
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	NetLog.info("Peer disconnected: %d" % peer_id)
	_peer_contexts.erase(peer_id)
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	NetLog.info("Connected to server as peer %d." % peer_id)

	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	NetLog.info("Disconnected from server.")
	server_disconnected.emit()
