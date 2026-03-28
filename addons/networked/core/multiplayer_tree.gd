@tool
class_name MultiplayerTree
extends Node

## The core networking wrapper that manages the multiplayer API and backend.
##
## This node bridges a specific [BackendPeer] (like ENet or WebSocket) with Godot's 
## native [SceneMultiplayer] API. It handles hosting, joining, and connection state.

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

## Indicates whether this specific tree instance is running as the server.
var is_server: bool

## The underlying network implementation (e.g., ENet, WebSocket, WebRTC).
## Duplicates the assigned resource at runtime to ensure isolated states.
@export var backend: BackendPeer:
	set(value):
		if not Engine.is_editor_hint():
			if backend:
				_disconnect_backend_signals()
			
			if value:
				backend = value.duplicate()
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

## The manager responsible for handling player lobbies, spawning, and game state.
@export var lobby_manager: MultiplayerLobbyManager:
	set(manager):
		if not Engine.is_editor_hint():
			if lobby_manager and configured.is_connected(lobby_manager.configured.emit):
				configured.disconnect(lobby_manager.configured.emit)
				
			lobby_manager = manager
			
			if lobby_manager and not configured.is_connected(lobby_manager.configured.emit):
				configured.connect(lobby_manager.configured.emit)
		else:
			lobby_manager = manager
			
		update_configuration_warnings()

## Direct access to the active [SceneMultiplayer] instance.
var multiplayer_api: SceneMultiplayer:
	get: return backend.api if backend else null

## Direct access to the active [MultiplayerPeer] connection state.
var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer if backend and backend.api else null


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	
	if not backend:
		warnings.append("A BackendPeer resource must be assigned to the 'backend' property.")
	elif backend.get_script() != null and backend.get_script().get_global_name() == "BackendPeer":
		warnings.append("The assigned backend is the abstract 'BackendPeer' class. Please assign a functional derived class.")
	elif backend:
		warnings.append_array(backend._get_backend_warnings(self))
		
	if not lobby_manager:
		warnings.append("A MultiplayerLobbyManager must be assigned to the 'lobby_manager' property.")
		
	return warnings


func _init() -> void:
	if not Engine.is_editor_hint():
		tree_exiting.connect(_on_exiting)


func _process(dt: float) -> void:
	if Engine.is_editor_hint():
		return
		
	if backend:
		backend.poll(dt)


## Initializes the network backend as a host/server and configures the multiplayer API.
func host() -> Error:
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			return setup_err
	
	var connection_code: Error = backend.host()
	
	if connection_code == OK:
		_config_api()
		
	return connection_code


## Attempts to join an active server at the given [param server_address].
## 
## Returns an error if the connection fails immediately, or [code]ERR_CANT_CONNECT[/code] 
## if the server does not respond within the timeout window.
func join(server_address: String, username: String) -> Error:
	backend.peer_reset_state()
	
	if backend.has_method("setup"):
		var setup_err: Error = backend.setup(self)
		if setup_err != OK:
			return setup_err
	
	var connection_code: Error = backend.join(server_address, username)
	
	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(connected_to_server, timer):
		return ERR_CANT_CONNECT
	
	if connection_code == OK:
		_config_api()
		
	return connection_code


## Returns [code]true[/code] if the peer is initialized and actively connected to a session.
func is_online() -> bool:
	return (multiplayer_peer != null 
		and not multiplayer_peer is OfflineMultiplayerPeer 
		and multiplayer_api != null 
		and multiplayer_api.has_multiplayer_peer())


# ------------------------------------------------------------------------------
# Internal Helpers & Callbacks
# ------------------------------------------------------------------------------

func _config_api() -> void:
	var multiplayer_root := lobby_manager.get_path() if lobby_manager else get_path()
	backend.configure_tree(get_tree(), multiplayer_root)
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
	if backend:
		backend.peer_reset_state()


func _on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	NetLog.info("Peer (%d) connected to server." % peer_id)

	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	server_disconnected.emit()
