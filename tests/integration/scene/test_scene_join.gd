## Integration tests for [MultiplayerSceneManager] join flow.
class_name TestLobbyJoin
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var level_builder: LevelBuilder


func before_test() -> void:
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	server_mgr = harness.server_scene_manager()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	harness.register_spawnable_scene(level_builder.packed)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_server_spawns_scene_after_host() -> void:
	# spawn_scenes() already ran synchronously inside host()
	var scenes := harness.server().api.scenes
	assert_that(scenes.scenes.size()).is_equal(1)

	var key := String(scenes.scenes.keys()[0])
	assert_that(key).is_equal(level_builder.scene_name)

	var spawned_scene: MultiplayerScene = scenes.scenes.values()[0]
	assert_that(spawned_scene).is_not_null()
	assert_that(spawned_scene is MultiplayerScene).is_true()
	assert_object(
		harness.server().api.get_service(MultiplayerSceneManager),
	).is_same(server_mgr)
	assert_object(scenes.scene(level_builder.scene_name)).is_same(spawned_scene)
	assert_object(scenes.scene_of(spawned_scene.level)).is_same(spawned_scene)


func test_two_clients_both_connect_to_server_with_scene() -> void:
	await harness.teardown()
	harness = make_unmanaged_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	await harness.add_client()
	await harness.add_client()

	for client in harness.clients():
		assert_that(client.is_online()).is_true()
