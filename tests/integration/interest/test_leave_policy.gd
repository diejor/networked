## Live three-participant coverage for [NetwInterestInterface.LeavePolicy].
class_name TestLeavePolicy
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
	fs.base_dir = create_temp_dir("leave_policy_saves")


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()
	probe_scene = _make_probe_scene()


func test_retain_freezes_and_readmission_resumes_same_node() -> void:
	var host := await game.add_host("valeria", true, _level_1_spawn())
	var observer := await game.add_client("jose", true, _level_1_spawn())
	var actor := await game.add_client("maria", true, _level_1_spawn())
	await game.wait_for_transitions()
	for runner in [host, observer, actor]:
		_mount_arena(runner.tree)
	await game.sync_ticks(2)

	var server_probe := probe_scene.instantiate() as NetwSpawnProbe
	server_probe.marker = "before"
	var entity := host.tree.api.replication.replicate(server_probe)
	host.tree.get_node("Arena").add_child(server_probe)
	var remote_probe := await _wait_probe(observer, entity.route)
	var actor_probe := await _wait_probe(actor, entity.route)
	assert_that(remote_probe).is_not_null()
	assert_that(actor_probe).is_not_null()
	var remote_instance := remote_probe.get_instance_id()
	var remote_entity := NetwEntity.of(remote_probe)
	var entered: Array = []
	var left: Array = []
	remote_entity.interest.on_enter(
		&"stealth",
		func(layer_id: StringName, peer_id: int):
			entered.append([layer_id, peer_id])
	)
	remote_entity.interest.on_leave(
		&"stealth",
		func(layer_id: StringName, peer_id: int):
			left.append([layer_id, peer_id])
	)

	var interest := host.tree.api.interest
	var layer := interest.layer(&"stealth")
	layer.default_leave_policy = NetwInterestInterface.LeavePolicy.RETAIN
	layer.add_viewer(observer.peer_id)
	layer.add_viewer(actor.peer_id)
	layer.add_entity(entity)
	interest.flush()
	await game.sync_ticks(8)
	entered.clear()
	left.clear()

	layer.remove_viewer(observer.peer_id)
	interest.flush()
	await game.sync_ticks(8)
	assert_array(left).contains_exactly([[&"stealth", observer.peer_id]])
	assert_bool(interest.has_filter(entity)).is_true()
	assert_bool(
		interest.participant_sees(observer.peer_id, entity),
	).is_false()
	assert_int(remote_probe.get_instance_id()).is_equal(remote_instance)
	server_probe.marker = "after"
	await game.sync_ticks(24)
	assert_str(remote_probe.marker).is_equal("before")
	assert_str(actor_probe.marker).is_equal("after")

	layer.add_viewer(observer.peer_id)
	interest.flush()
	await game.sync_ticks(24)
	assert_array(entered).contains_exactly([[&"stealth", observer.peer_id]])
	assert_int(remote_probe.get_instance_id()).is_equal(remote_instance)
	assert_str(remote_probe.marker).is_equal("after")


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)


func _mount_arena(mt: MultiplayerTree) -> void:
	var arena := Node.new()
	arena.name = "Arena"
	mt.add_child(arena)


func _wait_probe(
		runner: NetwSceneRunner,
		route: int,
) -> NetwSpawnProbe:
	for _i in 120:
		var node := runner.tree.api.liveness.node_of(route) as NetwSpawnProbe
		if node:
			return node
		await game.sync_ticks(1)
	return null


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "LeavePolicyProbe"
	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	var config := SceneReplicationConfig.new()
	var marker := NodePath(".:marker")
	config.add_property(marker)
	config.property_set_replication_mode(
		marker,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = config
	root.add_child(sync)
	sync.owner = root
	var path := NetwPathNamespace.next_path("interest", "LeavePolicyProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
