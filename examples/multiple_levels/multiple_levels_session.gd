class_name MultipleLevelsSession
extends Node2D

const LEVEL_1 := preload("res://examples/multiple_levels/Level1.tscn")
const LEVEL_2 := preload("res://examples/multiple_levels/Level2.tscn")
const PLAYER := preload("res://examples/multiple_levels/Player.tscn")
const START_POSE := Vector2(359, 70)
const START_STRIDE := Vector2(48, 0)

const SAVES := &"saves"
const WHERE := 0
const LEVEL := 1

var makers: Dictionary = {}
var levels: Dictionary = {}
var save_schema: NetwSchema

@onready var session: NetwSessionHandle = Netw.session(self)


func declare_save_schema() -> NetwSchema:
	var declared := Netw.configure_schema(&"multiple_levels_players")
	declared.replicated(false)
	declared.vector2(&"where")
	declared.string(&"level")
	return declared


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
	save_schema = declare_save_schema()
	Netw.configure_database(self, SAVES).backend(MemoryDatabase.new())
	Netw.database(self, SAVES).open(&"campaign")
	session.scene_live.connect(remember_level)
	session.scene_changed.connect(place_arrivals)
	get_tree().auto_accept_quit = false


func _exit_tree() -> void:
	NetwService.unregister(self)
	save_schema = null


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	await session.save_entities().wait()
	get_tree().quit()


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
	var tp: TPComponent = body.get_node("%TPComponent")
	tp.current_scene_path = LEVEL_1.resource_path
	open_level(LEVEL_1.resource_path).root.add_child(body)
	await NetwEntity.of(body).persistence.load().wait()
	if tp.current_scene_path != LEVEL_1.resource_path:
		Netw.reparent(body, open_level(tp.current_scene_path).root)
