## Tests [TPComponent] scene name resolution and path caching.
##
## These tests do NOT invoke networking. The teleport flow requires real peers
## and belongs in integration tests.
class_name TestTPComponent
extends NetwTestSuite

const TEST_LEVEL := "res://addons/networked_test/fixtures/TestLevel.tscn"


func test_scene_path_and_name_resolution_flow() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())

	assert_that(TPComponent._resolve_scene_name(TEST_LEVEL)).is_equal(
		"TestLevel",
	)
	assert_that(TPComponent._resolve_scene_name("")).is_equal("")

	tp.current_scene_path = TEST_LEVEL
	assert_that(tp.current_scene_name).is_equal("TestLevel")

	tp.current_scene_path = ""
	assert_that(tp.current_scene_name).is_equal("")


func test_spawn_and_start_scene_initialization_flow() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	tp.starting_scene_path = SceneNodePath.new(TEST_LEVEL + "::")
	assert_that(tp.current_scene_path).is_equal("")

	if tp.current_scene_path.is_empty():
		tp.current_scene_path = tp.starting_scene_path.scene_path

	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)
	assert_that(tp.current_scene_name).is_equal("TestLevel")

	tp = auto_free(TPComponent.new())
	tp.starting_scene_path = SceneNodePath.new(TEST_LEVEL + "::")
	var scene_mgr: MultiplayerSceneManager = auto_free(
		MultiplayerSceneManager.new(),
	)
	tp.spawn(scene_mgr)

	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)


func test_teleport_ignores_requests_while_busy() -> void:
	var tp: TPComponent = auto_free(TPComponent.new())
	await tp._tp_mutex.lock()
	var promise := tp.teleport(SceneNodePath.new(TEST_LEVEL + "::"))
	await get_tree().process_frame
	assert_that(promise.is_completed).is_true()
	tp._tp_mutex.unlock()

	tp = auto_free(TPComponent.new())
	tp._settle_until_msec = Time.get_ticks_msec() + 1000
	promise = tp.teleport(SceneNodePath.new(TEST_LEVEL + "::"))
	await get_tree().process_frame

	assert_that(promise.is_completed).is_true()


func test_parented_contributes_paths_without_node_owner() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var components := Node.new()
	components.name = "Components"
	root.add_child(components)

	var tp := TPComponent.new()
	components.add_child(tp)

	var save := SaveComponent.new()
	components.add_child(save)
	var spawner := MultiplayerEntity.new()
	components.add_child(spawner)

	save.finalize()

	var spawn_path := NodePath("Components/TPComponent:current_scene_path")
	var save_path := NodePath("TPComponent:current_scene_path")

	assert_that(spawner.replication_config.has_property(spawn_path)).is_true()
	assert_that(save.get_real_path(&"current_scene_path")).is_equal(save_path)
