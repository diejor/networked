class_name BomberGamestate
extends NetwService

const WORLD_SCENE := "res://examples/bomber/game/world.tscn"

signal game_error(what: String)

@onready var session: NetwSession = Netw.session(self)

var world: NetwScene:
	get:
		return session.find_scene(&"World")


func _ready() -> void:
	session.exited.connect(on_session_exited)


func on_session_exited(reason: NetwSession.ExitReason, detail: String) -> void:
	if reason != NetwSession.LOCAL_LEAVE:
		game_error.emit(detail if not detail.is_empty() else "Connection lost")


func begin_game() -> void:
	assert(multiplayer.is_server())
	Netw.change_scene_to_file(self, WORLD_SCENE).then(open_match)


func end_game() -> void:
	if multiplayer.is_server() and world != null:
		Netw.despawn(world.root)


func open_match(running: NetwScene) -> void:
	var players: Node = running.root.get_node(^"Players")
	for player: NetwPlayer in running.viewers:
		players.add_child(
			Netw.spawn_player(
				player,
				running.root.spawn_player,
				player.username,
				player.peer_id,
				players.get_child_count(),
			),
		)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
