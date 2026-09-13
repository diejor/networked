extends Node2D

const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")

const ENTRY_SPAWNER := ^"Player"

var level1: Node
var level2: Node


func _init() -> void:
	Netw.configure_clock(self)
	Netw.configure_spawn(spawn_level1)
	Netw.configure_spawn(spawn_level2)
	Netw.configure_join(self, spawn_player)
	Netw.configure_scene_requests(self, authorize_scene_request)


func spawn_level1() -> Node:
	return LEVEL_1.instantiate()


func spawn_level2() -> Node:
	return LEVEL_2.instantiate()


func open_level1() -> Node:
	if not is_instance_valid(level1):
		var world := Netw.spawn(spawn_level1)
		add_child(world)
		level1 = Netw.scene(world).root
	return level1


func open_level2() -> Node:
	if not is_instance_valid(level2):
		var world := Netw.spawn(spawn_level2)
		add_child(world)
		level2 = Netw.scene(world).root
	return level2


func authorize_scene_request(
		_participant: NetwParticipant,
		destination: String,
		_scope: int,
) -> Error:
	if destination == LEVEL_1.resource_path or destination == LEVEL_2.resource_path:
		return OK
	return ERR_UNAUTHORIZED


func spawn_player(participant: NetwParticipant) -> void:
	var level := open_level1()
	var spawner := NetwEntity.ensure(level.get_node(ENTRY_SPAWNER))
	var player := spawner.instantiate_player(participant)
	var engine := NetwEntity.of(player).persistence
	if engine:
		await engine.hydrate().wait()
	var tp: TPComponent = player.get_node("%TPComponent")
	tp.spawn(self)
