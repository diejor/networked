## Unit tests for MultiplayerLobbyManager's per-level configuration system.
##
## Verifies _set(), _get(), and _get_config() behavior without any multiplayer
## peers or scene tree — no networking is exercised here.
class_name TestLobbyManagerConfig
extends NetworkedTestSuite

var mgr: MultiplayerLobbyManager


func before_test() -> void:
	mgr = MultiplayerLobbyManager.new()
	# Not added to tree: avoids triggering multiplayer initialization.


func after_test() -> void:
	if is_instance_valid(mgr):
		mgr.free()


# --- _get_config defaults ---

func test_get_config_load_mode_defaults_to_on_startup() -> void:
	var config := mgr._get_config(&"UnknownLevel")
	assert_that(config["load_mode"]).is_equal(MultiplayerLobbyManager.LoadMode.ON_STARTUP)


func test_get_config_empty_action_defaults_to_freeze() -> void:
	var config := mgr._get_config(&"UnknownLevel")
	assert_that(config["empty_action"]).is_equal(MultiplayerLobbyManager.EmptyAction.FREEZE)


# --- _set ---

func test_set_returns_true_for_valid_config_property() -> void:
	assert_that(mgr._set(&"lobby_config/Level1/load_mode", 0)).is_true()


func test_set_returns_false_for_unrelated_property() -> void:
	assert_that(mgr._set(&"some_other_property", 42)).is_false()


func test_set_stores_load_mode_on_demand() -> void:
	mgr._set(&"lobby_config/Level1/load_mode", MultiplayerLobbyManager.LoadMode.ON_DEMAND)
	assert_that(mgr._get_config(&"Level1")["load_mode"]).is_equal(
		MultiplayerLobbyManager.LoadMode.ON_DEMAND)


func test_set_stores_empty_action_destroy() -> void:
	mgr._set(&"lobby_config/Level1/empty_action", MultiplayerLobbyManager.EmptyAction.DESTROY)
	assert_that(mgr._get_config(&"Level1")["empty_action"]).is_equal(
		MultiplayerLobbyManager.EmptyAction.DESTROY)


func test_set_stores_empty_action_keep_active() -> void:
	mgr._set(&"lobby_config/Level1/empty_action", MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)
	assert_that(mgr._get_config(&"Level1")["empty_action"]).is_equal(
		MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)


# --- _get ---

func test_get_returns_null_for_unrelated_property() -> void:
	assert_that(mgr._get(&"some_other_property")).is_null()


func test_get_returns_default_load_mode_when_not_set() -> void:
	assert_that(mgr._get(&"lobby_config/NewLevel/load_mode")).is_equal(
		MultiplayerLobbyManager.LoadMode.ON_STARTUP)


func test_get_returns_default_empty_action_when_not_set() -> void:
	assert_that(mgr._get(&"lobby_config/NewLevel/empty_action")).is_equal(
		MultiplayerLobbyManager.EmptyAction.FREEZE)


func test_get_reads_stored_load_mode() -> void:
	mgr._set(&"lobby_config/Level1/load_mode", MultiplayerLobbyManager.LoadMode.ON_DEMAND)
	assert_that(mgr._get(&"lobby_config/Level1/load_mode")).is_equal(
		MultiplayerLobbyManager.LoadMode.ON_DEMAND)


func test_get_reads_stored_empty_action() -> void:
	mgr._set(&"lobby_config/Level1/empty_action", MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)
	assert_that(mgr._get(&"lobby_config/Level1/empty_action")).is_equal(
		MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)


# --- Independence between levels ---

func test_two_levels_have_independent_load_modes() -> void:
	mgr._set(&"lobby_config/Level1/load_mode", MultiplayerLobbyManager.LoadMode.ON_DEMAND)
	mgr._set(&"lobby_config/Level2/load_mode", MultiplayerLobbyManager.LoadMode.ON_STARTUP)
	assert_that(mgr._get_config(&"Level1")["load_mode"]).is_equal(
		MultiplayerLobbyManager.LoadMode.ON_DEMAND)
	assert_that(mgr._get_config(&"Level2")["load_mode"]).is_equal(
		MultiplayerLobbyManager.LoadMode.ON_STARTUP)


func test_setting_one_level_does_not_affect_another() -> void:
	mgr._set(&"lobby_config/Level1/empty_action", MultiplayerLobbyManager.EmptyAction.DESTROY)
	var config2 := mgr._get_config(&"Level2")
	assert_that(config2["empty_action"]).is_equal(MultiplayerLobbyManager.EmptyAction.FREEZE)
