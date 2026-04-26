## Tests player spawning and lifecycle with real multiplayer peers.
##
## Uses NetworkTestHarness with a lobby manager and test level scene.
## Players are spawned via harness.spawn_player() which bypasses the RPC chain
## and directly calls lobby.add_player(), testing the server-side spawn path.
class_name TestPlayerSpawn
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE := preload("res://tests/helpers/TestPlayerMinimal.tscn")

var harness: NetworkTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)

	var server_mgr := harness._get_lobby_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
	await drain_frames(get_tree(), 3)


func test_spawned_player_is_in_lobby() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var lobby := harness.get_server_lobby()
	assert_that(player.get_parent()).is_equal(lobby.level)


func test_spawned_player_has_correct_authority() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var expected_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)


func test_spawned_player_has_username() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var client_comp: ClientComponent = player.get_node("%ClientComponent")
	assert_that(client_comp.username).is_equal("test_player_0")


func test_spawned_player_name_format() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.name).is_equal("test_player_0|%d" % peer_id)


func test_connect_client_called_on_spawn() -> void:
	var _player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var lobby := harness.get_server_lobby()
	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id)).is_true()


func test_two_players_in_same_lobby() -> void:
	harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	harness.spawn_player(client1, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client1, &"TestLevel")

	var lobby := harness.get_server_lobby()

	var peer_id_0 := client0.multiplayer_peer.get_unique_id()
	var peer_id_1 := client1.multiplayer_peer.get_unique_id()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id_0)).is_true()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id_1)).is_true()
