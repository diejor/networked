## Integration tests for [method MultiplayerSceneManager.spawn].
class_name TestLobbyCustomSpawn
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var client_mgr: MultiplayerSceneManager
var server_api: NetwMultiplayer
var server_core: SceneCore
var level_builder: LevelBuilder
var level_2_builder: LevelBuilder


func before_test() -> void:
	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	level_2_builder = LevelBuilder.new("TestLevel2") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_2_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	server_mgr = harness.server_scene_manager()
	server_api = harness.server().api
	server_core = server_api._scenes
	var client := await harness.add_client()
	client_mgr = harness.scene_manager_for(client)


func _set_spawn_fn(fn: Callable) -> void:
	server_mgr.level_spawn_function = fn
	client_mgr.level_spawn_function = fn


func test_spawn_function_receives_data_before_scene_enters_tree() -> void:
	var called := [false]
	var received = [null]
	var in_tree_during_call := [true]
	_set_spawn_fn(
		func(data: Variant) -> Node:
			called[0] = true
			received[0] = data
			var level := level_builder.packed.instantiate()
			in_tree_during_call[0] = level.is_inside_tree()
			return level
	)

	server_api.scene_spawn({ "round": 7 })

	assert_that(called[0]).is_true()
	assert_that(received[0]).is_equal({ "round": 7 })
	assert_that(in_tree_during_call[0]).is_false()
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()


func test_two_custom_spawns_register_independently() -> void:
	_set_spawn_fn(
		func(data: Variant) -> Node:
			return level_builder.packed.instantiate() if data == "level1" \
			else level_2_builder.packed.instantiate()
	)

	server_api.scene_spawn("level1")
	server_api.scene_spawn("level2")

	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()
	assert_that(server_api.scene(level_2_builder.scene_name) != null).is_true()


func test_activate_scene_uses_spawn_data_wakes_level_and_is_idempotent() -> void:
	var received = [null]
	var call_count := [0]
	_set_spawn_fn(
		func(data: Variant) -> Node:
			call_count[0] += 1
			received[0] = data
			return level_builder.packed.instantiate()
	)
	server_mgr.scene_spawn_data[level_builder.scene_name] = { "round": 3 }

	server_core.activate_scene(level_builder.scene_name)
	server_core.activate_scene(level_builder.scene_name)

	var scene := server_api.scene(level_builder.scene_name)
	assert_that(received[0]).is_equal({ "round": 3 })
	assert_that(call_count[0]).is_equal(1)
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_activate_scene_falls_back_to_name_when_no_spawn_data() -> void:
	var received = [null]
	_set_spawn_fn(
		func(data: Variant) -> Node:
			received[0] = data
			return level_builder.packed.instantiate()
	)

	server_core.activate_scene(level_builder.scene_name)

	assert_that(received[0]).is_equal(level_builder.scene_name)
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()


func test_custom_spawn_stays_active_while_initially_empty() -> void:
	_set_spawn_fn(
		func(_data: Variant) -> Node:
			return level_builder.packed.instantiate()
	)
	server_api.scene_spawn(level_builder.resource_path)
	await get_tree().process_frame

	var scene := server_api.scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)
	# A scene nobody has entered yet is a live scene, not a torn-down one.
	assert_bool(scene.peers.is_empty()).is_true()
