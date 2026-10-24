class_name BomberGamestate
extends NetwService

signal game_error(what: String)

@export var world_scene: PackedScene

@onready var session: NetwSession = Netw.session(self)

var world: NetwScene:
	get:
		return session.find_scene(&"World")


func _init() -> void:
	Netw.configure_spawn(make_world)


func _ready() -> void:
	session.exited.connect(on_session_exited)


func on_session_exited(reason: NetwSession.ExitReason, detail: String) -> void:
	if reason != NetwSession.LOCAL_LEAVE:
		game_error.emit(detail if not detail.is_empty() else "Connection lost")


func begin_game() -> void:
	assert(multiplayer.is_server())
	var level: Node = Netw.spawn(make_world)
	get_parent().add_child(level)
	var players: Node = level.get_node(^"Players")
	for player: NetwPlayer in session.players:
		players.add_child(
			Netw.spawn_player(
				player,
				level.spawn_player,
				player.username,
				player.peer_id,
				players.get_child_count(),
			),
		)


func end_game() -> void:
	if multiplayer.is_server() and world != null:
		Netw.despawn(world.root)


func make_world() -> Node:
	return world_scene.instantiate()


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
