## Integration tests for [MultiplayerSceneManager] scene lifecycle API.
class_name TestLobbyLifecycle
extends NetwTestSuite

const TEST_LEVEL_SCENE := preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)
const TEST_LEVEL_2_SCENE := preload(
	"res://addons/networked_test/fixtures/TestLevel2.tscn"
)

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager


func before_test() -> void:
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	server_mgr = harness.server_scene_manager()
	harness.register_spawnable_scene(TEST_LEVEL_SCENE)
	harness.register_spawnable_scene(TEST_LEVEL_2_SCENE)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	super.after_test()


func test_on_startup_scenes_spawned_after_host() -> void:
	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_true()
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_on_demand_scene_skipped_at_startup() -> void:
	var h2 := make_harness()
	await h2.setup(NetwTestSuite.create_scene_manager)
	h2.set_scene_policy(
		&"TestLevel2",
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
		MultiplayerSceneManager.EmptyAction.FREEZE
	)
	h2.register_spawnable_scene(TEST_LEVEL_SCENE)
	h2.register_spawnable_scene(TEST_LEVEL_2_SCENE)
	await h2.add_client()

	var mgr2 := h2.server_scene_manager()
	assert_that(mgr2.active_scenes.has(&"TestLevel")).is_true()
	assert_that(mgr2.active_scenes.has(&"TestLevel2")).is_false()
	await h2.teardown()


func test_preload_scene_populates_cache() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	var path := TEST_LEVEL_2_SCENE.resource_path
	server_mgr.preload_scene(&"TestLevel2")

	assert_that(path).is_not_empty()
	assert_that(server_mgr.is_scene_preloaded(&"TestLevel2")).is_true()


func test_preload_scene_does_not_instantiate() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	server_mgr.preload_scene(&"TestLevel2")

	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_false()


func test_spawn_after_preload_consumes_cache() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	var path := TEST_LEVEL_2_SCENE.resource_path
	server_mgr.preload_scene(&"TestLevel2")
	server_mgr.spawn_scene(&"TestLevel2")

	assert_that(path).is_not_empty()
	assert_that(server_mgr.is_scene_preloaded(&"TestLevel2")).is_false()
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_spawn_scene_adds_to_active_scenes() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	server_mgr.spawn_scene(&"TestLevel2")
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_spawn_scene_is_idempotent() -> void:
	server_mgr.spawn_scene(&"TestLevel")
	assert_that(server_mgr.active_scenes.size()).is_equal(2)


func test_activate_scene_spawns_missing_scene() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel2")
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_activate_scene_sets_level_process_mode_to_inherit() -> void:
	var scene := server_mgr.active_scenes[&"TestLevel"]
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED

	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_freeze_scene_sets_level_process_mode_disabled() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	server_mgr.freeze_scene(&"TestLevel")

	var scene := server_mgr.active_scenes[&"TestLevel"]
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_activate_after_freeze_restores_processing() -> void:
	server_mgr.freeze_scene(&"TestLevel")
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")

	var scene := server_mgr.active_scenes[&"TestLevel"]
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_freeze_scene_keeps_entry_in_active_scenes() -> void:
	server_mgr.freeze_scene(&"TestLevel")
	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_true()


func test_destroy_scene_removes_from_active_scenes() -> void:
	server_mgr.destroy_scene(&"TestLevel")
	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_false()


func test_destroy_scene_frees_the_scene_node() -> void:
	var scene := server_mgr.active_scenes[&"TestLevel"]
	server_mgr.destroy_scene(&"TestLevel")
	await get_tree().process_frame
	assert_that(is_instance_valid(scene)).is_false()


func test_destroy_then_spawn_recreates_scene() -> void:
	server_mgr.destroy_scene(&"TestLevel")
	await get_tree().process_frame

	server_mgr.spawn_scene(&"TestLevel")
	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_true()


func test_freeze_empty_action_disables_level_on_despawn() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var player := _join_player()
	player.queue_free()
	await wait_until(
		func(): return scene.level.process_mode == Node.PROCESS_MODE_DISABLED
	)

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_scene_on_despawn() -> void:
	server_mgr.set_scene_lifecycle_policy(
		&"TestLevel",
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
		MultiplayerSceneManager.EmptyAction.DESTROY
	)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var scene_ref: WeakRef = weakref(scene)
	var player := _join_player()
	player.queue_free()
	await wait_until(
		func(): return not server_mgr.active_scenes.has(&"TestLevel") \
			and not is_instance_valid(scene_ref.get_ref())
	)

	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_false()
	assert_that(is_instance_valid(scene_ref.get_ref())).is_false()


func test_keep_active_empty_action_leaves_level_processing() -> void:
	server_mgr.set_scene_lifecycle_policy(
		&"TestLevel",
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
		MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE
	)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var player := _join_player()
	player.queue_free()
	await wait_until(func(): return scene.connected_peers.is_empty())

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_nonempty_scene_not_frozen_by_empty_action() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var first_player := _add_scene_player(scene, 1001, &"first")
	var second_player := _add_scene_player(scene, 1002, &"second")

	first_player.queue_free()
	await wait_until(func(): return not scene.connected_peers.has(1001))

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	second_player.queue_free()


func _join_player() -> Node:
	var scene := server_mgr.active_scenes[&"TestLevel"] as MultiplayerScene
	return _add_scene_player(scene, 1001, &"test_player")


func _add_scene_player(
	scene: MultiplayerScene,
	peer_id: int,
	username: StringName,
) -> Node:
	var player := Node2D.new()
	NetwEntity.bundle(player, peer_id, username)
	scene.add_player(player)
	return player
