class_name MultipleLevelsSession
extends Node2D

const START_POSE := Vector2(359, 70)
const START_STRIDE := Vector2(48, 0)

@export var level_1: PackedScene
@export var level_2: PackedScene
@export var avatar: PackedScene

var makers: Dictionary = { }
var levels: Dictionary = { }

@onready var session: NetwSession = Netw.session(self)


func _init() -> void:
	Netw.configure_session(self).app(&"netw-example-multiple-levels") \
			.return_window(60.0)
	Netw.configure_clock(self)
	Netw.configure_database(self, &"saves").backend(
		preload("res://examples/multiple_levels/game/save_database.tres"),
	)
	Netw.configure_spawn(spawn_level1)
	Netw.configure_spawn(spawn_level2)
	Netw.configure_spawn(spawn_avatar)
	Netw.configure_join(spawn_player)


func _ready() -> void:
	makers[level_1.resource_path] = spawn_level1
	makers[level_2.resource_path] = spawn_level2
	NetwService.register(self)
	session.database(&"saves").open(&"campaign")
	session.scene_ready.connect(remember_level)
	get_tree().auto_accept_quit = false


func _exit_tree() -> void:
	NetwService.unregister(self)


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	await session.save_entities().wait()
	await session.leave().wait()
	get_tree().quit()


func spawn_level1() -> Node:
	return level_1.instantiate()


func spawn_level2() -> Node:
	return level_2.instantiate()


func spawn_avatar() -> Node:
	return avatar.instantiate()


func remember_level(level: NetwScene) -> void:
	levels[level.root.scene_file_path] = level.root


func open_level(path: String) -> NetwScene:
	var world: Node = levels.get(path)
	if not is_instance_valid(world):
		world = Netw.spawn(makers[path])
		add_child(world)
		levels[path] = world
	return Netw.scene(world)


func spawn_player(player: NetwPlayer) -> void:
	if not player.entities.is_empty():
		return
	var body := Netw.spawn_player(player, spawn_avatar)
	var roster: Array[NetwPlayer] = session.players
	(body as Node2D).position = START_POSE + START_STRIDE * roster.size()
	var tp: TPComponent = body.get_node("%TPComponent")
	tp.current_scene_path = level_1.resource_path
	await NetwEntity.of(body).persistence.load().wait()
	open_level(tp.current_scene_path).root.add_child(body)
