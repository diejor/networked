## Tests player spawning and lifecycle with real multiplayer peers.
##
## Uses NetworkTestHarness with a lobby manager and test level scene.
## Players are spawned via harness.spawn_player() which bypasses the RPC chain
## and directly calls lobby.add_player(), testing the server-side spawn path.
class_name TestPlayerSpawn
extends GdUnitTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE := preload("res://tests/helpers/TestPlayerMinimal.tscn")

var harness: NetworkTestHarness


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(2, LOBBY_MANAGER_SCENE)

	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	await harness.connect_all()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame

	SaveComponent.registered_components.clear()
	TPComponent._pending.clear()


func test_spawned_player_is_in_lobby() -> void:
	var player := harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	var lobby := harness.get_server_lobby()
	assert_that(player.get_parent()).is_equal(lobby.level)


func test_spawned_player_has_correct_authority() -> void:
	var player := harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	var expected_id := harness.get_client(0).multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)


func test_spawned_player_has_username() -> void:
	var player := harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	var client_comp: ClientComponent = player.get_node("%ClientComponent")
	assert_that(client_comp.username).is_equal("test_player_0")


func test_spawned_player_name_format() -> void:
	var player := harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	var peer_id := harness.get_client(0).multiplayer_peer.get_unique_id()
	assert_that(player.name).is_equal("test_player_0|%d" % peer_id)


func test_connect_client_called_on_spawn() -> void:
	var _player := harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	var lobby := harness.get_server_lobby()
	var peer_id := harness.get_client(0).multiplayer_peer.get_unique_id()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id)).is_true()


func test_two_players_in_same_lobby() -> void:
	harness.spawn_player(0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(0, &"TestLevel")
	
	harness.spawn_player(1, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(1, &"TestLevel")
	
	var lobby := harness.get_server_lobby()

	var peer_id_0 := harness.get_client(0).multiplayer_peer.get_unique_id()
	var peer_id_1 := harness.get_client(1).multiplayer_peer.get_unique_id()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id_0)).is_true()
	assert_that(lobby.synchronizer.connected_clients.has(peer_id_1)).is_true()
