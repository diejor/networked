class_name MultiplayerLobbyManager
extends MultiplayerSpawner

const SERVER_LOBBY = preload("uid://dga0loylsa26i")
const CLIENT_LOBBY = preload("uid://cr2k17cu45app")

const TP_CANVAS_LAYER = preload("uid://bs4ebh48fcoxt")
var tp_canvas: CanvasLayer

var active_lobbies: Dictionary[StringName, Lobby]

@export_file var lobbies: Array[String]

func _ready() -> void:
	spawn_function = spawn_lobby
	spawn_path = "."
	


func spawn_lobbies() -> void:
	if multiplayer.is_server():
		for level_path: String in lobbies:
			spawn(level_path)


func spawn_lobby(level_file_path: String) -> Node:
	var level_scene: PackedScene = load(level_file_path)
	var level: Node = level_scene.instantiate()
	
	var lobby_scene: PackedScene = SERVER_LOBBY if multiplayer.is_server() else CLIENT_LOBBY
	
	var lobby: Lobby = lobby_scene.instantiate()
	
	lobby.level = level
	active_lobbies[level.name] = lobby
	
	return lobby


@rpc("any_peer", "call_remote", "reliable")
func request_join_player(
	client_data_bytes: PackedByteArray) -> void:
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(client_data_bytes)
	for client: ClientComponent in get_tree().get_nodes_in_group("clients"):
		client.player_joined.emit(client_data)


func _on_configured() -> void:
	var peer: MultiplayerTree = get_parent()
	if peer.is_server:
		spawn_lobbies()
	else:
		tp_canvas = TP_CANVAS_LAYER.instantiate()
		add_child(tp_canvas)
	pass
