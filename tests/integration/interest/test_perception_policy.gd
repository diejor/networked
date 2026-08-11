## Live host and retained-client coverage for local perception policy.
class_name TestPerceptionPolicy
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const DATABASE := preload(
	"res://examples/quick_start/quick_start_database.tres"
)
const LEVEL_1_SPAWN := "uid://bqi7mvxdnvgch::Player"

var game: NetwGameHarness
var probe_scene: PackedScene


func before() -> void:
	var fs := DATABASE.backend as FileSystemDatabase
	fs.base_dir = create_temp_dir("perception_policy_saves")


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()
	probe_scene = _make_probe_scene()


func test_host_and_retained_client_share_hide_applier() -> void:
	var host := await game.add_host("valeria", true, _level_1_spawn())
	var observer := await game.add_client("jose", true, _level_1_spawn())
	var actor := await game.add_client("maria", true, _level_1_spawn())
	await game.wait_for_transitions()
	for runner in [host, observer, actor]:
		_mount_arena(runner.tree)
	await game.sync_ticks(2)

	var server_probe := probe_scene.instantiate()
	var entity := host.tree.api._replication.replicate(server_probe)
	host.tree.get_node("Arena").add_child(server_probe)
	var observer_probe := await _wait_probe(observer, entity.route)
	var actor_probe := await _wait_probe(actor, entity.route)
	assert_that(observer_probe).is_not_null()
	assert_that(actor_probe).is_not_null()
	var server_visual := server_probe.get_node("Visual") as Node2D
	var observer_visual := observer_probe.get_node("Visual") as Node2D
	var actor_visual := actor_probe.get_node("Visual") as Node2D

	var interest := host.tree.api._interest
	var layer := interest.layer(&"stealth")
	layer.default_leave_policy = NetwMultiplayer.LeavePolicy.RETAIN
	layer.add_viewer(observer.peer_id)
	layer.add_viewer(actor.peer_id)
	layer.add_entity(entity)
	interest.flush_now()
	await game.sync_ticks(8)

	assert_bool(interest.wire_admits(host.peer_id, entity)).is_true()
	assert_bool(interest.participant_sees(host.peer_id, entity)).is_false()
	assert_bool(server_visual.visible).is_false()
	assert_bool(observer_visual.visible).is_true()
	assert_bool(actor_visual.visible).is_true()

	layer.add_viewer(host.peer_id)
	interest.flush_now()
	await game.sync_ticks(4)
	assert_bool(interest.participant_sees(host.peer_id, entity)).is_true()
	assert_bool(server_visual.visible).is_true()

	var observer_instance := observer_probe.get_instance_id()
	layer.remove_viewer(observer.peer_id)
	interest.flush_now()
	await game.sync_ticks(8)

	assert_int(observer_probe.get_instance_id()).is_equal(observer_instance)
	assert_bool(observer_visual.visible).is_false()
	assert_bool(server_visual.visible).is_true()
	assert_bool(actor_visual.visible).is_true()

	layer.add_viewer(observer.peer_id)
	interest.flush_now()
	await game.sync_ticks(8)
	assert_int(observer_probe.get_instance_id()).is_equal(observer_instance)
	assert_bool(observer_visual.visible).is_true()


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)


func _mount_arena(mt: MultiplayerTree) -> void:
	var arena := Node.new()
	arena.name = "Arena"
	mt.add_child(arena)


func _wait_probe(runner: NetwSceneRunner, route: int) -> Node:
	for _i in 120:
		var node := runner.tree.api._liveness.node_of(route)
		if node:
			return node
		await game.sync_ticks(1)
	return null


func _make_probe_scene() -> PackedScene:
	var root := Node.new()
	root.name = "PerceptionPolicyProbe"
	var visual := Node2D.new()
	visual.name = "Visual"
	root.add_child(visual)
	visual.owner = root
	NetwEntity.ensure(root)
	var path := NetwPathNamespace.next_path(
		"interest",
		"PerceptionPolicyProbe",
	)
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
