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
	# spawn() takes the session. A bare session with no
	# registered scene resolves current_scene_path from starting_scene_path and
	# skips the player add, which is all this path asserts.
	var api := NetwMultiplayer.new()
	tp.spawn(api)

	assert_that(tp.current_scene_path).is_equal(TEST_LEVEL)
	api.embedding.dispose()


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

	var spawner := Node.new()
	components.add_child(spawner)

	# current_scene_path rides the spawn packet through its .on_spawn() mark and
	# is a persisted column with no per-tick sync axis, so it rides no lane.
	var cfg: NetwScriptModel.PropertyConfig = \
			NetwScriptModel.get_node_property_configs(tp).get(&"current_scene_path")
	assert_that(cfg).is_not_null()
	assert_that(cfg.is_spawn_state).is_true()
	assert_that(cfg.is_persisted).is_true()
	assert_that(cfg.in_state_set).is_false()
	assert_that(cfg.in_input_set).is_false()
