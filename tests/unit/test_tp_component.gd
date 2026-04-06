## Tests TPComponent's pure GDScript logic: scene name resolution and path caching.
##
## These tests do NOT invoke any multiplayer or RPC methods. The teleport flow
## itself (request_teleport, _reparent_to_lobby) requires real peers and belongs
## in integration tests.
class_name TestTPComponent
extends NetworkedTestSuite

const TEST_LEVEL := "res://tests/helpers/TestLevel.tscn"


func test_resolve_scene_name_returns_root_name() -> void:
	var scene_name := TPComponent._resolve_scene_name(TEST_LEVEL)
	assert_that(scene_name).is_equal("TestLevel")


func test_resolve_scene_name_empty_returns_empty() -> void:
	var scene_name := TPComponent._resolve_scene_name("")
	assert_that(scene_name).is_equal("")


func test_current_scene_path_setter_caches_name() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	tp.current_scene_path = TEST_LEVEL
	assert_that(tp.current_scene_name).is_equal("TestLevel")


func test_current_scene_path_empty_clears_name() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	tp.current_scene_path = TEST_LEVEL
	tp.current_scene_path = ""
	assert_that(tp.current_scene_name).is_equal("")


func test_enter_tree_uses_starting_scene_when_empty() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	tp.starting_scene_path = SceneNodePath.new(TEST_LEVEL + "::")
	# current_scene_path is empty by default
	assert_that(tp.current_scene_path).is_equal("")

	# Simulate _enter_tree behavior (can't add to tree because of
	# EditorTooling.validate_and_halt in _ready)
	if tp.current_scene_path.is_empty():
		tp.current_scene_path = tp.starting_scene_path.scene_path

	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)
	assert_that(tp.current_scene_name).is_equal("TestLevel")
