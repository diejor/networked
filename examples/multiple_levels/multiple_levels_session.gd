class_name MultipleLevelsSession
extends Node2D

const LEVEL_1 := preload("res://examples/multiple_levels/Level1.tscn")
const LEVEL_2 := preload("res://examples/multiple_levels/Level2.tscn")
const PLAYER := preload("res://examples/multiple_levels/Player.tscn")
const START_POSE := Vector2(359, 70)
const START_STRIDE := Vector2(48, 0)

var makers: Dictionary = {}
var levels: Dictionary = {}

@onready var session: NetwSessionHandle = Netw.session(self)


func _init() -> void:
	makers[LEVEL_1.resource_path] = spawn_level1
	makers[LEVEL_2.resource_path] = spawn_level2
	Netw.configure_clock(self)
	Netw.configure_spawn(spawn_level1)
	Netw.configure_spawn(spawn_level2)
	Netw.configure_spawn(spawn_avatar)
	Netw.configure_join(spawn_player)
	Netw.configure_scene_requests(authorize_scene_request)


func _ready() -> void:
	NetwService.register(self)
	session.scene_live.connect(remember_level)
	session.scene_changed.connect(place_arrivals)


func _exit_tree() -> void:
	NetwService.unregister(self)


func spawn_level1() -> Node:
	return LEVEL_1.instantiate()


func spawn_level2() -> Node:
	return LEVEL_2.instantiate()


func spawn_avatar() -> Node:
	return PLAYER.instantiate()


func remember_level(level: NetwSceneHandle) -> void:
	levels[level.root.scene_file_path] = level.root


func place_arrivals(
		level: NetwSceneHandle,
		arrived: Array[NetwPlayer],
) -> void:
	for player in arrived:
		place_player(player, level)


func place_player(player: NetwPlayer, level: NetwSceneHandle) -> void:
	var avatar := Netw.spawn_player(player, spawn_avatar)
	var roster: Array[NetwPlayer] = session.players
	(avatar as Node2D).position = START_POSE + START_STRIDE * roster.size()
	level.root.add_child(avatar)


func open_level(path: String) -> NetwSceneHandle:
	var world: Node = levels.get(path)
	if not is_instance_valid(world):
		world = Netw.spawn(makers[path])
		add_child(world)
		levels[path] = world
	return Netw.scene(world)


func authorize_scene_request(
		_player: NetwPlayer,
		destination: String,
		_scope: Netw.SceneChange,
) -> Error:
	if (
			destination == LEVEL_1.resource_path
			or destination == LEVEL_2.resource_path
	):
		return OK
	return ERR_UNAUTHORIZED


func spawn_player(player: NetwPlayer) -> void:
	open_level(LEVEL_1.resource_path)
	var body := Netw.spawn_player(player, spawn_avatar)
	var roster: Array[NetwPlayer] = session.players
	(body as Node2D).position = START_POSE + START_STRIDE * roster.size()
	await NetwEntity.of(body).persistence.hydrate().wait()
	var tp: TPComponent = body.get_node("%TPComponent")
	open_level(tp.current_scene_path).root.add_child(body)
