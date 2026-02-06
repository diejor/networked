class_name LobbyManager
extends MultiplayerSpawner

@export_file("*.tscn") var lobbies: Array[String]
@export_file("*.tscn") var world_server_path: String
@export_file("*.tscn") var world_client_path: String

var active_lobbies: Dictionary[StringName, Node]

func _ready() -> void:
	spawn_function = spawn_lobby
	spawn_lobbies.call_deferred()


func spawn_lobbies() -> void:
	if multiplayer.is_server():
		for lobby_path: String in lobbies:
			spawn(lobby_path)


func spawn_lobby(lobby_file_path: String) -> Node:
	var lobby_scene: PackedScene = load(lobby_file_path)
	var lobby: Node = lobby_scene.instantiate()
	
	var world_path: String
	if multiplayer.is_server():
		world_path = world_server_path
	else:
		world_path = world_client_path
	var world_scene: PackedScene = load(world_path)
	var world: Node = world_scene.instantiate()
	
	var scene_spawner: MultiplayerSpawner = world.get_node("%SceneSpawner")
	scene_spawner.spawn_path = "../" + lobby.name
	
	world.name = lobby.name + world.name
	world.add_child(lobby)
	lobby.owner = world
	active_lobbies[lobby.name] = lobby
	
	return world


@rpc("any_peer", "call_remote", "reliable")
func request_join_player(
	client_data: Dictionary) -> void:
	for client: ClientComponent in get_tree().get_nodes_in_group("clients"):
		client.player_joined.emit(client_data)
