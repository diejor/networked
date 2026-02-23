class_name MultiplayerLobbyManager
extends MultiplayerSpawner

signal configured

const SERVER_LOBBY = preload("uid://dga0loylsa26i")
const CLIENT_LOBBY = preload("uid://cr2k17cu45app")
const VIEWPORTS_DEBUG = preload("uid://xu4dh3epglir")

@export var tp_layer: TPLayer:
	set(layer):
		configured.connect(layer.configured.emit)
		tp_layer = layer

var active_lobbies: Dictionary[StringName, Lobby]

var lobbies: Array[String]:
	get:
		if lobbies.is_empty():
			assert(get_spawnable_scene_count() > 0, "Add lobbies to the spawn list.")
			for scene_idx in get_spawnable_scene_count():
				lobbies.append(get_spawnable_scene(scene_idx))
			clear_spawnable_scenes()
		return lobbies


func _init() -> void:
	configured.connect(_on_configured)


func _ready() -> void:
	spawn_function = spawn_lobby
	spawn_path = "."
	add_to_group("lobby_managers")


func _on_lobby_spawned(node: Node) -> void:
	var lobby := node as Lobby
	active_lobbies[lobby.level.name] = lobby


func _on_lobby_despawned(node: Node) -> void:
	var lobby := node as Lobby
	active_lobbies.erase(lobby.level.name)


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
	lobby.tree_entered.connect(_on_lobby_spawned.bind(lobby))
	lobby.tree_exiting.connect(_on_lobby_despawned.bind(lobby))
	
	return lobby


@rpc("any_peer", "call_remote", "reliable")
func request_join_player(
	client_data_bytes: PackedByteArray) -> void:
	var client_data: MultiplayerClientData = MultiplayerClientData.new()
	client_data.deserialize(client_data_bytes)
	for client: ClientComponent in get_tree().get_nodes_in_group("clients"):
		client.player_joined.emit(client_data)


func _on_server_disconnected() -> void:
	for lobby: Lobby in active_lobbies.values():
		lobby.get_parent().remove_child(lobby)


func _on_configured() -> void:
	if not multiplayer.server_disconnected.is_connected(_on_server_disconnected):
		multiplayer.server_disconnected.connect(_on_server_disconnected)
	
	if multiplayer.is_server():
		var debug_viewports: ViewportDebug = VIEWPORTS_DEBUG.instantiate()
		child_entered_tree.connect(debug_viewports._on_node_entered)
		child_exiting_tree.connect(debug_viewports._on_node_exited)
		add_child(debug_viewports)
		
		spawn_lobbies()
