## Integration tests for [MultiplayerSceneManager] join flow.
class_name TestLobbyJoin
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)
	server_mgr = harness._get_scene_manager(harness.get_server())
	# Scenes must be registered before add_client() because host() runs
	# synchronously inside host() during _on_configured().
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_server_spawns_scene_after_host() -> void:
	# spawn_scenes() already ran synchronously inside host()
	assert_that(server_mgr.active_scenes.size()).is_equal(1)


func test_active_scene_key_is_level_name() -> void:
	var key := String(server_mgr.active_scenes.keys()[0])
	assert_that(key).is_equal("TestLevel")


func test_spawned_scene_is_scene_instance() -> void:
	var spawned_scene: MultiplayerScene = server_mgr.active_scenes.values()[0]
	assert_that(spawned_scene).is_not_null()
	assert_that(spawned_scene is MultiplayerScene).is_true()


func test_two_clients_both_connect_to_server_with_scene() -> void:
	harness.queue_free()
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)
	harness._get_scene_manager(harness.get_server()).add_spawnable_scene(
		TEST_LEVEL_SCENE.resource_path)
	await harness.add_client()
	await harness.add_client()

	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()
