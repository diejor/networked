class_name GameServer
extends Node

signal peer_impl_changed()
signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)

signal configured

var backend: MultiplayerServerBackend = Networked.get_config().server_backend
@export var scene_manager: LobbyManager


var multiplayer_api: SceneMultiplayer:
	get: return backend.api
var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer
var root: String: 
	get: return multiplayer_api.root_path
	
func _ready() -> void:
	multiplayer_api.peer_connected.connect(on_peer_connected)
	multiplayer_api.peer_disconnected.connect(on_peer_disconnected)


func init() -> Error:
	backend.peer_reset_state()
	var connection_code: Error = backend.create_server()
	if connection_code == OK:
		config_api()

	return connection_code


func config_api() -> void:
	assert(is_instance_valid(scene_manager), 
		"Server lobbies node is missing before configuration.")
	backend.configure_tree(get_tree(), scene_manager.get_path())
	configured.emit()


func on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)


func on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func _process(dt: float) -> void:
	if backend:
		backend.poll(dt)
