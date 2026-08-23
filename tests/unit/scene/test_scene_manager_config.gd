## Unit tests for [MultiplayerSceneManager] declaration snapshotting.
##
## The manager holds no runtime state. It only snapshots its exported rows into
## a [NetwSceneConfig] for [NetwMultiplayer] to read.
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


func test_a_declared_scene_is_keyed_by_the_stem_the_live_book_uses() -> void:
	var path := "res://tests/support/scene/marked_test_scene.tscn"
	var packed: PackedScene = load(path)
	var stem := NetwMultiplayerCore.scene_packed_stem(packed)
	mgr.register_scene_path(path)

	var config := mgr._build_netw_scene_config()

	assert_str(String(stem)).is_not_equal(path.get_file().get_basename())
	assert_object(config.declared_scene(stem)).is_not_null()


func test_scene_config_snapshots_isolation() -> void:
	mgr.scene_isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD

	var config := mgr._build_netw_scene_config()

	assert_int(config.isolation).is_equal(NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD)


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
