## Integration tests for [method MultiplayerSceneManager.spawn].
class_name TestLobbyCustomSpawn
extends NetwTestSuite


var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var client_mgr: MultiplayerSceneManager
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
	await harness.setup(NetwTestSuite.create_scene_manager)
	server_mgr = harness.server_scene_manager()
	var client := await harness.add_client()
	client_mgr = harness.scene_manager_for(client)


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()


func _set_spawn_fn(fn: Callable) -> void:
	server_mgr.level_spawn_function = fn
	client_mgr.level_spawn_function = fn


func test_level_spawn_function_called_on_spawn() -> void:
	var called := [false]
	_set_spawn_fn(func(_data: Variant) -> Node:
		called[0] = true
		return level_builder.packed.instantiate()
	)

	server_mgr.spawn(level_builder.resource_path)

	assert_that(called[0]).is_true()


func test_level_spawn_function_receives_correct_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return level_builder.packed.instantiate()
	)

	server_mgr.spawn({"round": 7})

	assert_that(received[0]).is_equal({"round": 7})


func test_level_not_in_tree_when_spawn_function_called() -> void:
	var in_tree_during_call := [true]
	_set_spawn_fn(func(_data: Variant) -> Node:
		var level := level_builder.packed.instantiate()
		in_tree_during_call[0] = level.is_inside_tree()
		return level
	)

	server_mgr.spawn(level_builder.resource_path)

	assert_that(in_tree_during_call[0]).is_false()


func test_custom_spawn_scene_enters_active_scenes() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return level_builder.packed.instantiate()
	)

	server_mgr.spawn(level_builder.resource_path)

	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()


func test_two_custom_spawns_register_independently() -> void:
	_set_spawn_fn(func(data: Variant) -> Node:
		return level_builder.packed.instantiate() if data == "level1" \
			else level_2_builder.packed.instantiate()
	)

	server_mgr.spawn("level1")
	server_mgr.spawn("level2")

	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()
	assert_that(server_mgr.active_scenes.has(level_2_builder.scene_name)).is_true()


func test_activate_scene_uses_scene_spawn_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return level_builder.packed.instantiate()
	)
	server_mgr.scene_spawn_data[level_builder.scene_name] = {"round": 3}

	server_mgr.activate_scene(level_builder.scene_name)

	assert_that(received[0]).is_equal({"round": 3})
	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()


func test_activate_scene_falls_back_to_name_when_no_spawn_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return level_builder.packed.instantiate()
	)

	server_mgr.activate_scene(level_builder.scene_name)

	assert_that(received[0]).is_equal(level_builder.scene_name)
	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()


func test_activate_scene_wakes_level_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return level_builder.packed.instantiate()
	)
	server_mgr.scene_spawn_data[level_builder.scene_name] = level_builder.scene_name

	server_mgr.activate_scene(level_builder.scene_name)

	var scene := server_mgr.active_scenes.get(level_builder.scene_name) as MultiplayerScene
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_activate_scene_does_not_respawn_when_already_active() -> void:
	var call_count := [0]
	_set_spawn_fn(func(_data: Variant) -> Node:
		call_count[0] += 1
		return level_builder.packed.instantiate()
	)
	server_mgr.scene_spawn_data[level_builder.scene_name] = level_builder.scene_name

	server_mgr.activate_scene(level_builder.scene_name)
	server_mgr.activate_scene(level_builder.scene_name)

	assert_that(call_count[0]).is_equal(1)


func test_freeze_empty_action_applied_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return level_builder.packed.instantiate()
	)

	server_mgr.spawn(level_builder.resource_path)
	await get_tree().process_frame

	var scene := server_mgr.active_scenes.get(level_builder.scene_name) as MultiplayerScene
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_scene_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return level_builder.packed.instantiate()
	)
	server_mgr.set_scene_lifecycle_policy(
		level_builder.scene_name,
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
		MultiplayerSceneManager.EmptyAction.DESTROY
	)

	server_mgr.spawn(level_builder.resource_path)
	await get_tree().process_frame

	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_false()


func test_keep_active_empty_action_leaves_level_processing_after_custom_spawn(
) -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return level_builder.packed.instantiate()
	)
	server_mgr.set_scene_lifecycle_policy(
		level_builder.scene_name,
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
		MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE
	)

	server_mgr.spawn(level_builder.resource_path)
	await get_tree().process_frame

	var scene := server_mgr.active_scenes.get(level_builder.scene_name) as MultiplayerScene
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)
