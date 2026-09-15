extends Node2D

const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")
const PLAYER := preload("res://examples/quick_start/Player.tscn")
const START_POSE := Vector2(359, 70)
const START_STRIDE := Vector2(48, 0)

var level1: Node


func _init() -> void:
	Netw.configure_clock(self)
	Netw.configure_spawn(spawn_level1)
	Netw.configure_spawn(spawn_level2)
	Netw.configure_spawn(spawn_avatar)
	Netw.configure_join(self, spawn_player)
	Netw.configure_scene_requests(self, authorize_scene_request)


func spawn_level1() -> Node:
	return LEVEL_1.instantiate()


func spawn_level2() -> Node:
	return LEVEL_2.instantiate()


func spawn_avatar(username: StringName, peer_id: int) -> Node:
	return NetwEntity.bind(PLAYER.instantiate(), username, peer_id)


func open_level1() -> Node:
	if not is_instance_valid(level1):
		var world := Netw.spawn(spawn_level1)
		add_child(world)
		level1 = Netw.scene(world).root
	return level1


func authorize_scene_request(
		_participant: NetwParticipant,
		destination: String,
		_scope: int,
) -> Error:
	if destination == LEVEL_1.resource_path or destination == LEVEL_2.resource_path:
		return OK
	return ERR_UNAUTHORIZED


func spawn_player(participant: NetwParticipant) -> void:
	open_level1()
	var player := Netw.spawn_player(
		participant, spawn_avatar, participant.username, participant.peer_id
	)
	var roster: Array[NetwParticipant] = Netw.session(self).participants
	(player as Node2D).position = START_POSE + START_STRIDE * roster.size()
	var restored: Error = await NetwEntity.of(player).persistence.hydrate().wait()
	if not participant.is_active:
		player.queue_free()
		return
	if restored != OK:
		push_error(
			"quick start: %s was not restored (error %d), so nothing is placed."
			% [participant.username, restored]
		)
		player.queue_free()
		return
	var tp: TPComponent = player.get_node("%TPComponent")
	var recipe := load(tp.current_scene_path) as PackedScene
	Netw.session(self).activate_scene(recipe).root.add_child(player)
