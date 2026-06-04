## Unit tests for [MultiplayerSceneManager] level configuration.
##
## Covers the editor-integration path (`_set` / `_get` with
## `scene_config/...` property strings) and the public
## [method MultiplayerSceneManager.set_scene_lifecycle_policy] API,
## both of which must write through to the same internal config.
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

#region Public lifecycle policy

# Unconfigured levels read back the documented defaults via both the
# typed [_get_config] dictionary and the property-string [_get] path.
func test_default_config_for_unconfigured_level() -> void:
	var config := mgr._get_config(&"UnknownLevel")
	assert_that(config["load_mode"]).is_equal(
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
	)
	assert_that(config["empty_action"]).is_equal(
		MultiplayerSceneManager.EmptyAction.FREEZE,
	)
	assert_that(mgr._get(&"scene_config/NewLevel/load_mode")).is_equal(
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
	)
	assert_that(mgr._get(&"scene_config/NewLevel/empty_action")).is_equal(
		MultiplayerSceneManager.EmptyAction.FREEZE,
	)


# Both write paths (Phase C public API + the editor `_set`) must produce
# identical results across the enum cross-product.
@warning_ignore("unused_parameter")
func test_lifecycle_policy_round_trip(
		use_public_api: bool,
		load_mode: int,
		empty_action: int,
		test_parameters := [
			[
				true,
				MultiplayerSceneManager.LoadMode.ON_DEMAND,
				MultiplayerSceneManager.EmptyAction.DESTROY,
			],
			[
				true,
				MultiplayerSceneManager.LoadMode.ON_STARTUP,
				MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE,
			],
			[
				false,
				MultiplayerSceneManager.LoadMode.ON_DEMAND,
				MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE,
			],
			[
				false,
				MultiplayerSceneManager.LoadMode.ON_STARTUP,
				MultiplayerSceneManager.EmptyAction.DESTROY,
			],
		],
) -> void:
	if use_public_api:
		mgr.set_scene_lifecycle_policy(&"Level1", load_mode, empty_action)
	else:
		mgr._set(&"scene_config/Level1/load_mode", load_mode)
		mgr._set(&"scene_config/Level1/empty_action", empty_action)

	var config := mgr._get_config(&"Level1")
	assert_that(config["load_mode"]).is_equal(load_mode)
	assert_that(config["empty_action"]).is_equal(empty_action)
	assert_that(mgr._get(&"scene_config/Level1/load_mode")).is_equal(load_mode)
	assert_that(mgr._get(&"scene_config/Level1/empty_action")).is_equal(
		empty_action,
	)

#endregion

#region Editor property routing

# Property-string routing: scene_config/... is owned by this class, anything
# else is rejected by `_set` and returns null from `_get`.
@warning_ignore("unused_parameter")
func test_property_routing(
		prop: StringName,
		expected_set: bool,
		test_parameters := [
			[&"scene_config/Level1/load_mode", true],
			[&"some_other_property", false],
		],
) -> void:
	assert_that(mgr._set(prop, 0)).is_equal(expected_set)
	if not expected_set:
		assert_that(mgr._get(prop)).is_null()


func test_levels_are_independent() -> void:
	mgr.set_scene_lifecycle_policy(
		&"Level1",
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
		MultiplayerSceneManager.EmptyAction.DESTROY,
	)
	# Level2 stays unconfigured -> reads documented defaults.
	var config1 := mgr._get_config(&"Level1")
	var config2 := mgr._get_config(&"Level2")

	assert_that(config1["load_mode"]).is_equal(
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
	)
	assert_that(config1["empty_action"]).is_equal(
		MultiplayerSceneManager.EmptyAction.DESTROY,
	)
	assert_that(config2["load_mode"]).is_equal(
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
	)
	assert_that(config2["empty_action"]).is_equal(
		MultiplayerSceneManager.EmptyAction.FREEZE,
	)

#endregion
