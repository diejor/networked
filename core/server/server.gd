class_name GameServer
extends Node


signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)


@export var backend: MultiplayerServerBackend

const SCENE_MANAGER: PackedScene = preload("uid://d3ag2052swfwd")
@onready var scene_manager: LobbyManager = SCENE_MANAGER.instantiate()


var multiplayer_api: SceneMultiplayer:
	get: return backend.multiplayer_api
var multiplayer_peer: MultiplayerPeer:
	get: return backend.multiplayer_peer
var root: String: 
	get: return multiplayer_api.root_path


func _ready() -> void:
	multiplayer_api.peer_connected.connect(on_peer_connected)
	multiplayer_api.peer_disconnected.connect(on_peer_disconnected)

	var server_err := init()
	assert(server_err == OK or server_err == ERR_ALREADY_IN_USE,
		"Dedicated server failed to start: %s" % error_string(server_err))
	


func init() -> Error:
	backend.peer_reset_state()
	var err: Error = backend.create_server()
	if err != OK:
		return err

	add_child(scene_manager)
	config_api()
	return OK


func config_api() -> void:
	assert(is_instance_valid(scene_manager), 
		"Server lobbies node is missing before configuration.")
	backend.configure_tree(get_tree(), scene_manager.get_path())


func on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)


func on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func _process(dt: float) -> void:
	if backend:
		backend.poll(dt)
