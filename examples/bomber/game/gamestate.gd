class_name BomberGamestate
extends NetwService

const WORLD_SCENE := "res://examples/bomber/game/world.tscn"
const LOBBY_SCENE := "res://examples/bomber/game/lobby_level.tscn"

signal game_ended()
signal game_error(what: String)

@onready var session: NetwSessionHandle = Netw.session(self)

var world: NetwSceneHandle:
	get:
		return Netw.scene(self, &"World")

var lobby: NetwSceneHandle:
	get:
		return Netw.scene(self, &"Lobby")


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)
	Netw.configure_spawn(spawn_world)
	Netw.configure_join(place_player)


func _ready() -> void:
	session.entered.connect(open_lobby)
	session.disconnected.connect(on_server_disconnected)


func spawn_lobby() -> Node:
	var packed: PackedScene = load(LOBBY_SCENE)
	return packed.instantiate()


func spawn_world() -> Node:
	var packed: PackedScene = load(WORLD_SCENE)
	return packed.instantiate()


func open_lobby() -> void:
	if not multiplayer.is_server():
		return
	get_parent().add_child(Netw.spawn(spawn_lobby))


func place_player(player: NetwPlayer) -> void:
	lobby.watch(player)


func on_server_disconnected() -> void:
	game_error.emit("Server disconnected")
	game_ended.emit()


func begin_game() -> void:
	assert(multiplayer.is_server())
	get_parent().add_child(Netw.spawn(spawn_world))


func end_game() -> void:
	if multiplayer.is_server() and world != null:
		close_match(world)

	game_ended.emit()


func open_match(running: NetwSceneHandle) -> void:
	var players: Node = running.root.get_node(^"Players")
	for player: NetwPlayer in session.players:
		if player.is_active:
			players.add_child(
				Netw.spawn_player(
					player,
					running.root.spawn_player,
					player.username,
					player.peer_id,
					players.get_child_count(),
				),
			)
			lobby.unwatch(player)


func close_match(running: NetwSceneHandle) -> void:
	for player: NetwPlayer in session.players:
		if player.is_active:
			lobby.watch(player)
	Netw.despawn(running.root)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
