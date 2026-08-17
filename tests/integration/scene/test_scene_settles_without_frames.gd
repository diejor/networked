## Law suite for local scene presentation under pump drive alone: a scene
## spawned between two pumps starts being presented, and one destroyed between
## two pumps stops, with no idle frame anywhere in the run.
##
## The rig pumps the session directly through [method MultiplayerAPI.poll] and
## never yields a frame, so anything the scene machine puts on the frame's own
## deferred queue simply never runs. That is the point: what a peer presents is
## recomputed by the pump the session already drives, and a session that runs
## without rendering is one a headless host does every day.
##
## A listen server is the peer under test because presentation is what it has:
## a dedicated server resolves [member SceneCore.current_scene] to
## [code]null[/code] by role, so it can never witness the recompute.
class_name TestSceneSettlesWithoutFrames
extends NetwTestSuite

var harness: NetwTestHarness
var host: MultiplayerTree
var level: LevelBuilder
var second_level: LevelBuilder


func before_test() -> void:
	level = LevelBuilder.new("SettleLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level.pack()
	second_level = LevelBuilder.new("SettleLevelTwo") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	second_level.pack()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level.packed)
	harness.register_spawnable_scene(second_level.packed)
	host = await harness.add_listen_server(
		harness.make_sceneless_payload("settle_host"),
	)
	await drain_frames(get_tree(), 2)


func test_a_scene_spawned_between_pumps_is_presented_with_no_frame() -> void:
	assert_that(_scenes().current_scene).is_null()

	_scenes().spawn_scene(level.scene_name)
	_pump(2)

	assert_that(_scenes().current_scene).is_not_null()


func test_a_scene_destroyed_between_pumps_stops_being_presented() -> void:
	_scenes().spawn_scene(level.scene_name)
	_pump(2)
	assert_that(_scenes().current_scene).is_not_null()

	_scenes().destroy(level.scene_name)
	_pump(2)

	assert_that(_scenes().current_scene).is_null()


func test_a_retired_scene_is_freed_by_its_linger_of_pumps() -> void:
	_scenes().spawn_scene(level.scene_name)
	_pump(2)
	var container := _scenes().current_scene
	assert_that(container).is_not_null()

	_scenes().retire(level.scene_name, 3)
	_pump(2)
	assert_bool(container.is_queued_for_deletion()).is_false()

	_pump(1)

	assert_bool(container.is_queued_for_deletion()).is_true()


# Two live scenes under different stems are what makes the seat readable at
# all: with one, the seat and the fall-back-to-a-live-scene answer name the
# same scene and nothing can tell them apart.
func test_a_seated_host_presents_the_scene_it_sits_in() -> void:
	_scenes().spawn_scene(level.scene_name)
	_scenes().spawn_scene(second_level.scene_name)
	_pump(2)

	var first := host.api.scene_find(level.scene_name)
	var second := host.api.scene_find(second_level.scene_name)
	assert_bool(first.is_valid()).is_true()
	assert_bool(second.is_valid()).is_true()
	assert_that(host.api.scene_get_current()).is_equal(first)

	assert_int(host.api.scene_admit(second, host.api.get_unique_id())) \
			.is_equal(OK)
	_pump(2)

	assert_that(host.api.scene_get_current()).is_equal(second)


func _pump(rounds: int) -> void:
	for _i in rounds:
		host.multiplayer.poll()


func _scenes() -> SceneCore:
	return host.api._scenes
