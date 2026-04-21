class_name TestLobbyJoin
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerLobbyManager


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)
	server_mgr = harness._get_lobby_manager(harness.get_server())
	# Scenes must be registered before add_client() because spawn_lobbies()
	# runs synchronously inside host() during _on_configured().
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame


func test_server_spawns_lobby_after_host() -> void:
	# spawn_lobbies() already ran synchronously inside host()
	assert_that(server_mgr.active_lobbies.size()).is_equal(1)


func test_active_lobby_key_is_level_name() -> void:
	var key := String(server_mgr.active_lobbies.keys()[0])
	assert_that(key).is_equal("TestLevel")


func test_spawned_lobby_is_lobby_instance() -> void:
	var spawned_lobby: Lobby = server_mgr.active_lobbies.values()[0]
	assert_that(spawned_lobby).is_not_null()
	assert_that(spawned_lobby is Lobby).is_true()


func test_two_clients_both_connect_to_server_with_lobby() -> void:
	harness.queue_free()
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)
	harness._get_lobby_manager(harness.get_server()).add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	await harness.add_client()
	await harness.add_client()

	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()
