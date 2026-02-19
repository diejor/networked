class_name MultiplayerTree
extends Node

signal configured()
signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)
signal connected_to_server()

@export var is_server: bool

@export var backend: BackendPeer

@export var lobby_manager: MultiplayerLobbyManager


var multiplayer_api: SceneMultiplayer:
	get: return backend.api if backend else null

var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer if backend else null

var uid: int:
	get: return multiplayer_api.get_unique_id() if multiplayer_api else 0


func _init() -> void:
	configured.connect(_on_configured)

func _ready() -> void:
	configured.connect(lobby_manager._on_configured)
	


func _on_configured() -> void:
	# These fire for both host and clients
	multiplayer_api.peer_connected.connect(_on_peer_connected)
	multiplayer_api.peer_disconnected.connect(_on_peer_disconnected)
	
	# This only fires on clients. It's safe to connect on the host; it just won't trigger.
	multiplayer_api.connected_to_server.connect(_on_connected_to_server)


func host() -> Error:
	backend.peer_reset_state()
	var connection_code: Error = backend.host()
	
	if connection_code == OK:
		_config_api()
		
	return connection_code


func join(server_address: String, username: String) -> Error:
	backend.peer_reset_state()
	var connection_code: Error = backend.join(server_address, username)
	
	if connection_code == OK:
		_config_api()
		await connected_to_server
		
	return connection_code


func _config_api() -> void:
	backend.configure_tree(get_tree(), lobby_manager.get_path())
	configured.emit()


func _on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	print("Peer (%d) connected to server." % uid)

	set_multiplayer_authority(uid, false) 
	connected_to_server.emit()


func _process(dt: float) -> void:
	if backend:
		backend.poll(dt)
