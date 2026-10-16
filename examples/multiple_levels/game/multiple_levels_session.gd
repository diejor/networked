class_name MultipleLevelsSession
extends Node2D

const START_POSE := Vector2(359, 70)
const START_STRIDE := Vector2(48, 0)

@export var level_1: PackedScene
@export var level_2: PackedScene
@export var avatar: PackedScene
@export var saves: NetwDatabaseBackend

var makers: Dictionary = { }
var levels: Dictionary = { }

@onready var session: NetwSession = Netw.session(self)


func _init() -> void:
	Netw.configure_session(self).app(&"netw-example-multiple-levels")
	Netw.configure_clock(self)
	Netw.configure_spawn(spawn_level1)
	Netw.configure_spawn(spawn_level2)
	Netw.configure_spawn(spawn_avatar)
	Netw.configure_join(spawn_player)


func _ready() -> void:
	makers[level_1.resource_path] = spawn_level1
	makers[level_2.resource_path] = spawn_level2
	NetwService.register(self)
	Netw.configure_database(self, &"saves").backend(saves)
	session.database(&"saves").open(&"campaign")
	session.scene_ready.connect(remember_level)
	get_tree().auto_accept_quit = false


func _exit_tree() -> void:
	NetwService.unregister(self)


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	await session.save_entities().wait()
	get_tree().quit()


func spawn_level1() -> Node:
	return level_1.instantiate()


func spawn_level2() -> Node:
	return level_2.instantiate()


func spawn_avatar() -> Node:
	return avatar.instantiate()


func remember_level(level: NetwScene) -> void:
	levels[level.root.scene_file_path] = level.root
	if multiplayer.is_server():
		level.viewer_entered.connect(place_player.bind(level))


func place_player(player: NetwPlayer, level: NetwScene) -> void:
	if not player.bodies.is_empty():
		return
	var new_avatar := Netw.spawn_player(player, spawn_avatar)
	var roster: Array[NetwPlayer] = session.players
	(new_avatar as Node2D).position = START_POSE + START_STRIDE * roster.size()
	level.root.add_child(new_avatar)


func open_level(path: String) -> NetwScene:
	var world: Node = levels.get(path)
	if not is_instance_valid(world):
		world = Netw.spawn(makers[path])
		add_child(world)
		levels[path] = world
	return Netw.scene(world)


func spawn_player(player: NetwPlayer) -> void:
	var start := level_1.resource_path
	open_level(start)
	var body := Netw.spawn_player(player, spawn_avatar)
	var roster: Array[NetwPlayer] = session.players
	(body as Node2D).position = START_POSE + START_STRIDE * roster.size()
	var tp: TPComponent = body.get_node("%TPComponent")
	tp.current_scene_path = start
	open_level(start).root.add_child(body)
	await NetwEntity.of(body).persistence.load().wait()
	if tp.current_scene_path != start:
		Netw.reparent(body, open_level(tp.current_scene_path).root)
