class_name LobbyManager
extends MultiplayerSpawner

@export_file("*.tscn") var server_lobby_path: String
@export_file("*.tscn") var client_lobby_path: String

var active_lobbies: Dictionary[StringName, Lobby]

func get_levels() -> Array[String]:
	return Networked.get_config().levels


func _ready() -> void:
	spawn_function = spawn_lobby
	assert(not get_levels().is_empty(), "No levels to replicate. Add levels to\
`{node}`.".format({node=name}))
	
	if owner is GameServer:
		var server: GameServer = owner
		if not server.multiplayer_peer is OfflineMultiplayerPeer:
			spawn_lobbies.call_deferred()
		server.configured.connect(spawn_lobbies)


func spawn_lobbies() -> void:
	if multiplayer.is_server():
		for level_path: String in get_levels():
			spawn(level_path)


func spawn_lobby(level_file_path: String) -> Node:
	var level_scene: PackedScene = load(level_file_path)
	var level: Node = level_scene.instantiate()
	
	var lobby_scene: PackedScene = load(
		server_lobby_path if multiplayer.is_server() else client_lobby_path
	)
	
	var lobby: Lobby = lobby_scene.instantiate()
	
	var scene_spawner: MultiplayerSpawner = lobby.scene_spawner
	scene_spawner.spawn_path = "../" + level.name
	
	lobby.level = level
	active_lobbies[level.name] = lobby
	
	return lobby


@rpc("any_peer", "call_remote", "reliable")
func request_join_player(
	client_data: Dictionary) -> void:
	for client: ClientComponent in get_tree().get_nodes_in_group("clients"):
		client.player_joined.emit(client_data)
