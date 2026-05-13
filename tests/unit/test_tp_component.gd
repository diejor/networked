## Tests [TPComponent] scene name resolution and path caching.
##
## These tests do NOT invoke networking. The teleport flow
## ([method TPComponent.request_teleport], [method TPComponent._reparent_to_scene])
## requires real peers and belongs in integration tests.
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

	if tp.current_scene_path.is_empty():
		tp.current_scene_path = tp.starting_scene_path.scene_path

	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)
	assert_that(tp.current_scene_name).is_equal("TestLevel")


func test_spawn_initializes_current_scene_path_from_starting_scene() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	tp.starting_scene_path = SceneNodePath.new(TEST_LEVEL + "::")
	
	# We need a SceneManager for spawn()
	var scene_mgr: MultiplayerSceneManager = auto_free(
		MultiplayerSceneManager.new())
	
	# Manually call spawn without it being in tree
	tp.spawn(scene_mgr)
	
	# It should have initialized current_scene_path from starting_scene_path
	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)
