## Unit tests for [MultiplayerSceneManager] level configuration.
##
## Covers declaration resources and wrapper construction.
class_name TestSceneManagerConfig
extends NetwTestSuite

var mgr: MultiplayerSceneManager


func before_test() -> void:
	mgr = MultiplayerSceneManager.new()
	# Not added to tree: avoids triggering multiplayer initialization.


func after_test() -> void:
	if is_instance_valid(mgr):
		mgr.free()
	await super.after_test()

#region Editor property routing

func test_scene_uid_resolves_to_its_resource_path() -> void:
	var path := "res://addons/networked_test/fixtures/TestLevel.tscn"
	var uid := ResourceUID.id_to_text(ResourceLoader.get_resource_uid(path))

	mgr.register_scene_path(uid)

	assert_array(mgr.get_configured_paths()).contains([path])


func test_scene_config_snapshots_concurrency() -> void:
	mgr.concurrency = NetwSceneConfig.Concurrency.CONCURRENT

	var config := mgr._build_netw_scene_config()

	assert_int(config.concurrency).is_equal(
		NetwSceneConfig.Concurrency.CONCURRENT,
	)


func test_scene_config_snapshots_initial_scenes() -> void:
	var path := "res://addons/networked_test/fixtures/TestLevel.tscn"
	mgr.initial_scene_paths = [path]

	var config := mgr._build_netw_scene_config()

	assert_int(config.initial_scenes.size()).is_equal(1)
	assert_str(config.initial_scenes[0].resource_path).is_equal(path)


func test_single_mode_rejects_a_second_active_scene() -> void:
	mgr.concurrency = NetwSceneConfig.Concurrency.SINGLE
	var active := MultiplayerScene.new()
	active.name = &"Scene"
	var level := Node.new()
	level.name = &"First"
	active.level = level
	mgr.active_scenes[&"First"] = active
	var accepted := mgr._can_spawn_scene(&"Second")

	assert_bool(accepted).is_false()
	active.free()


#endregion

#region Wrapper construction

func test_single_mode_builds_a_plain_wrapper() -> void:
	mgr.concurrency = NetwSceneConfig.Concurrency.SINGLE
	var scene: Variant = mgr._make_scene_wrapper(true)

	assert_object(scene).is_instanceof(MultiplayerScene)
	assert_bool(scene is SubViewport).is_false()
	assert_object(scene.gate).is_instanceof(InterestGate)

	scene.free()


func test_concurrent_host_builds_an_isolated_wrapper() -> void:
	mgr.concurrency = NetwSceneConfig.Concurrency.CONCURRENT
	var scene: Variant = mgr._make_scene_wrapper(true)

	assert_object(scene).is_instanceof(MultiplayerScene)
	assert_bool(scene is SubViewport).is_true()
	assert_bool((scene as SubViewport).own_world_3d).is_true()
	assert_that((scene as SubViewport).render_target_update_mode).is_equal(
		SubViewport.UPDATE_DISABLED,
	)

	scene.free()


func test_concurrent_client_builds_a_plain_wrapper() -> void:
	mgr.concurrency = NetwSceneConfig.Concurrency.CONCURRENT
	var scene: Variant = mgr._make_scene_wrapper(false)

	assert_object(scene).is_instanceof(MultiplayerScene)
	assert_bool(scene is SubViewport).is_false()

	scene.free()

#endregion
