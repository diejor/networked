## Integration tests for MultiplayerSceneManager's scene lifecycle API.
##
## Covers LoadMode startup behaviour, the full public API (preload_scene,
## spawn_scene, activate_scene, freeze_scene, destroy_scene), and the
## automatic EmptyAction logic triggered when the last player leaves.
class_name TestLobbyLifecycle
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)
	server_mgr = harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	server_mgr.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


# --- LoadMode.ON_STARTUP ---

func test_on_startup_scenes_spawned_after_host() -> void:
	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_true()
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_on_demand_scene_skipped_at_startup() -> void:
	# Spin up a fresh harness with TestLevel2 configured as ON_DEMAND before host.
	var h2: NetworkTestHarness = auto_free(NetworkTestHarness.new())
	add_child(h2)
	await h2.setup(NetworkedTestSuite.create_scene_manager)

	var mgr2 := h2._get_scene_manager(h2.get_server())
	mgr2._scene_configs[&"TestLevel2"] = {
		"load_mode": MultiplayerSceneManager.LoadMode.ON_DEMAND,
		"empty_action": MultiplayerSceneManager.EmptyAction.FREEZE,
	}
	mgr2.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	mgr2.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)
	await h2.add_client()

	assert_that(mgr2.active_scenes.has(&"TestLevel")).is_true()
	assert_that(mgr2.active_scenes.has(&"TestLevel2")).is_false()


# --- preload_scene ---

func test_preload_scene_populates_cache() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	var path := TEST_LEVEL_2_SCENE.resource_path
	server_mgr.preload_scene(&"TestLevel2")

	assert_that(server_mgr._scene_cache.has(path)).is_true()


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

	assert_that(server_mgr._scene_cache.has(path)).is_false()
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


# --- spawn_scene ---

func test_spawn_scene_adds_to_active_scenes() -> void:
	server_mgr.destroy_scene(&"TestLevel2")
	await get_tree().process_frame

	server_mgr.spawn_scene(&"TestLevel2")
	assert_that(server_mgr.active_scenes.has(&"TestLevel2")).is_true()


func test_spawn_scene_is_idempotent() -> void:
	server_mgr.spawn_scene(&"TestLevel")
	assert_that(server_mgr.active_scenes.size()).is_equal(2)


# --- activate_scene ---

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


# --- freeze_scene ---

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


# --- destroy_scene ---

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


# --- EmptyAction auto-trigger ---
# These tests emit the SceneSynchronizer.despawned signal directly so they do
# not require a real multiplayer player. The connected_clients dict is empty by
# default (no players have joined in before_test), so every _apply call fires.

func test_freeze_empty_action_disables_level_on_despawn() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var dummy := Node.new()
	scene.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_scene_on_despawn() -> void:
	server_mgr._set(&"scene_config/TestLevel/empty_action",
		MultiplayerSceneManager.EmptyAction.DESTROY)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var dummy := Node.new()
	scene.synchronizer.despawned.emit(dummy)
	dummy.free()
	await get_tree().process_frame

	assert_that(server_mgr.active_scenes.has(&"TestLevel")).is_false()
	assert_that(is_instance_valid(scene)).is_false()


func test_keep_active_empty_action_leaves_level_processing() -> void:
	server_mgr._set(&"scene_config/TestLevel/empty_action",
		MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]

	var dummy := Node.new()
	scene.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_nonempty_scene_not_frozen_by_empty_action() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(&"TestLevel")
	var scene := server_mgr.active_scenes[&"TestLevel"]
	# Simulate a connected client so the scene is considered non-empty.
	scene.synchronizer.connected_clients[999] = true

	var dummy := Node.new()
	scene.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	scene.synchronizer.connected_clients.erase(999)
