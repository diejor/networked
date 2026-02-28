class_name MultiplayerTree
extends Node

signal configured()
signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)
signal connected_to_server()
signal server_disconnected()

var is_server: bool

@export var backend: BackendPeer:
	set(value):
		backend = value.duplicate()
		multiplayer_api.peer_connected.connect(_on_peer_connected)
		multiplayer_api.peer_disconnected.connect(_on_peer_disconnected)
		multiplayer_api.connected_to_server.connect(_on_connected_to_server)
		multiplayer_api.server_disconnected.connect(_on_server_disconnected)

@export var lobby_manager: MultiplayerLobbyManager:
	set(manager):
		configured.connect(manager.configured.emit)
		lobby_manager = manager


var multiplayer_api: SceneMultiplayer:
	get: return backend.api if backend else null

var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer if backend else null


func _init() -> void:
	tree_exiting.connect(_on_exiting)

func _on_exiting() -> void:
	backend.peer_reset_state()


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
		
		var timer := get_tree().create_timer(2.)
		if await Async.timeout(connected_to_server, timer):
			return Error.ERR_CANT_CONNECT
		
	return connection_code


func _config_api() -> void:
	backend.configure_tree(get_tree(), lobby_manager.get_path())
	configured.emit()


func _on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)


func _on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func _on_connected_to_server() -> void:
	var peer_id := multiplayer_peer.get_unique_id()
	print("Peer (%d) connected to server." % peer_id)

	set_multiplayer_authority(peer_id, false) 
	connected_to_server.emit()


func _on_server_disconnected() -> void:
	server_disconnected.emit()

func _process(dt: float) -> void:
	if backend:
		backend.poll(dt)


func is_online() -> bool:
	return (not multiplayer_peer is OfflineMultiplayerPeer 
		and multiplayer_api.has_multiplayer_peer())
