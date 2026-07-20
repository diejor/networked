## Unit tests for [MultiplayerSceneManager] declaration snapshotting.
##
## The manager holds no runtime state. It only snapshots its exported rows into
## a [NetwSceneConfig] for [NetwSceneInterface] to read.
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


func test_scene_config_carries_the_level_spawn_function() -> void:
	var fn := func(_data: Variant) -> Node: return Node.new()
	mgr.level_spawn_function = fn

	var config := mgr._build_netw_scene_config()

	assert_bool(config.level_spawn_function == fn).is_true()
