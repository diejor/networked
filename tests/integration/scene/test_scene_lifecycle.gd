## Integration tests for [MultiplayerSceneManager] scene lifecycle API.
class_name TestLobbyLifecycle
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
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
	harness.register_spawnable_scene(level_builder.packed)
	harness.register_spawnable_scene(level_2_builder.packed)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_startup_scenes_are_active_and_idempotent() -> void:
	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()
	assert_that(server_mgr.active_scenes.has(level_2_builder.scene_name)).is_true()

	server_mgr.spawn_scene(level_builder.scene_name)
	assert_that(server_mgr.active_scenes.size()).is_equal(2)

	server_mgr.freeze_scene(level_builder.scene_name)
	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()


func test_on_demand_scene_skipped_at_startup() -> void:
	var h2 := make_unmanaged_harness()
	await h2.setup(NetwTestSuite.create_scene_manager)
	h2.set_scene_policy(
		level_2_builder.scene_name,
		MultiplayerSceneManager.LoadMode.ON_DEMAND,
		MultiplayerSceneManager.EmptyAction.FREEZE,
	)
	h2.register_spawnable_scene(level_builder.packed)
	h2.register_spawnable_scene(level_2_builder.packed)
	await h2.add_client()

	var mgr2 := h2.server_scene_manager()
	assert_that(mgr2.active_scenes.has(level_builder.scene_name)).is_true()
	assert_that(mgr2.active_scenes.has(level_2_builder.scene_name)).is_false()
	await h2.teardown()


func test_preload_caches_without_instantiating_and_spawn_consumes() -> void:
	server_mgr.destroy_scene(level_2_builder.scene_name)
	await get_tree().process_frame

	var path := level_2_builder.resource_path
	server_mgr.preload_scene(level_2_builder.scene_name)

	assert_that(path).is_not_empty()
	assert_that(
		server_mgr.is_scene_preloaded(level_2_builder.scene_name),
	).is_true()
	assert_that(
		server_mgr.active_scenes.has(level_2_builder.scene_name),
	).is_false()

	server_mgr.spawn_scene(level_2_builder.scene_name)

	assert_that(
		server_mgr.is_scene_preloaded(level_2_builder.scene_name),
	).is_false()
	assert_that(server_mgr.active_scenes.has(level_2_builder.scene_name)).is_true()


func test_spawn_scene_adds_to_active_scenes() -> void:
	server_mgr.destroy_scene(level_2_builder.scene_name)
	await get_tree().process_frame

	server_mgr.spawn_scene(level_2_builder.scene_name)
	assert_that(server_mgr.active_scenes.has(level_2_builder.scene_name)).is_true()


func test_activate_spawns_missing_scene_and_updates_processing() -> void:
	server_mgr.destroy_scene(level_2_builder.scene_name)
	await get_tree().process_frame

	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_2_builder.scene_name)
	assert_that(server_mgr.active_scenes.has(level_2_builder.scene_name)).is_true()

	var scene := server_mgr.active_scenes[level_builder.scene_name]
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED

	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	server_mgr.freeze_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)

	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_destroy_frees_and_spawn_recreates_scene() -> void:
	var scene := server_mgr.active_scenes[level_builder.scene_name]
	server_mgr.destroy_scene(level_builder.scene_name)
	await get_tree().process_frame

	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_false()
	assert_that(is_instance_valid(scene)).is_false()

	server_mgr.spawn_scene(level_builder.scene_name)
	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_true()


func test_freeze_empty_action_disables_level_on_despawn() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)
	var scene := server_mgr.active_scenes[level_builder.scene_name]

	var player := _join_player()
	player.queue_free()
	await wait_until(
		func(): return scene.level.process_mode == Node.PROCESS_MODE_DISABLED
	)

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_scene_on_despawn() -> void:
	server_mgr.set_scene_lifecycle_policy(
		level_builder.scene_name,
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
		MultiplayerSceneManager.EmptyAction.DESTROY,
	)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)
	var scene := server_mgr.active_scenes[level_builder.scene_name]

	var scene_ref: WeakRef = weakref(scene)
	var player := _join_player()
	player.queue_free()
	await wait_until(
		func():
			return not server_mgr.active_scenes.has(level_builder.scene_name) \
					and not is_instance_valid(scene_ref.get_ref())
	)

	assert_that(server_mgr.active_scenes.has(level_builder.scene_name)).is_false()
	assert_that(is_instance_valid(scene_ref.get_ref())).is_false()


func test_keep_active_empty_action_leaves_level_processing() -> void:
	server_mgr.set_scene_lifecycle_policy(
		level_builder.scene_name,
		MultiplayerSceneManager.LoadMode.ON_STARTUP,
		MultiplayerSceneManager.EmptyAction.KEEP_ACTIVE,
	)
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)
	var scene := server_mgr.active_scenes[level_builder.scene_name]

	var player := _join_player()
	player.queue_free()
	await wait_until(func(): return scene.connected_peers.is_empty())

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_nonempty_scene_not_frozen_by_empty_action() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_scene(level_builder.scene_name)
	var scene := server_mgr.active_scenes[level_builder.scene_name]

	var first_player := _add_scene_player(scene, 1001, &"first")
	var second_player := _add_scene_player(scene, 1002, &"second")

	first_player.queue_free()
	await wait_until(func(): return not scene.connected_peers.has(1001))

	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	second_player.queue_free()


func _join_player() -> Node:
	var scene := (
			server_mgr.active_scenes[level_builder.scene_name] as MultiplayerScene
	)
	return _add_scene_player(scene, 1001, &"test_player")


func _add_scene_player(
		scene: MultiplayerScene,
		peer_id: int,
		username: StringName,
) -> Node:
	var player := Node2D.new()
	NetwEntity.bundle(player, peer_id, username)
	scene.add_player(player)
	return player
