class_name GameClient
extends Node


signal connected_to_server()
signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)


@export var backend: MultiplayerClientBackend
@export var scene_manager: LobbyManager


var multiplayer_api: SceneMultiplayer:
	get: return backend.api
var multiplayer_peer: MultiplayerPeer:
	get: return backend.api.multiplayer_peer
var uid: int:
	get: return multiplayer_api.get_unique_id()
	set(value): push_warning("Client UID should not be set directly.")


func _ready() -> void:
	multiplayer_api.peer_connected.connect(on_peer_connected)
	multiplayer_api.peer_disconnected.connect(on_peer_disconnected)
	multiplayer_api.connected_to_server.connect(on_connected_to_server)


func connect_client(server_address: String, _username: String) -> Error:
	var code: Error = init(server_address, _username)
	await connected_to_server
	return code


func init(server_address: String, _username: String) -> Error:
	backend.peer_reset_state()
	var connection_code: Error = backend.create_connection(server_address, _username)
	if connection_code == OK:
		config_api()

	return connection_code


func config_api() -> void:
	backend.configure_tree(get_tree(), scene_manager.get_path())


func on_peer_connected(peer_id: int) -> void:
	peer_connected.emit(peer_id)

func on_peer_disconnected(peer_id: int) -> void:
	peer_disconnected.emit(peer_id)


func on_connected_to_server() -> void:
	print("Client (%d) connected to server." % multiplayer_api.get_unique_id())
	set_multiplayer_authority(multiplayer_api.get_unique_id(), false)
	connected_to_server.emit()


func _process(dt: float) -> void:
	if backend:
		backend.poll(dt)
