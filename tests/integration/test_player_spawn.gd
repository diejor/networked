## Tests player spawning and lifecycle with real multiplayer peers.
##
## Uses [NetworkTestHarness] with a scene manager and test level scene.
## Players are spawned via [method NetworkTestHarness.spawn_player] which
## bypasses the RPC chain and directly calls [method MultiplayerScene.add_player],
## testing the server-side spawn path.
class_name TestPlayerSpawn
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE := preload("res://tests/helpers/TestPlayerMinimal.tscn")

var harness: NetworkTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	var server_mgr := harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_spawned_player_is_in_scene() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var scene := harness.get_server_scene()
	assert_that(player.get_parent()).is_equal(scene.level)


func test_spawned_player_has_correct_authority() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var expected_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)


func test_spawned_player_has_username() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var client_comp := SpawnerComponent.unwrap(player)
	assert_that(client_comp.entity_id).is_equal("test_player_0")


func test_spawned_player_name_format() -> void:
	var player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.name).is_equal("test_player_0|%d" % peer_id)


func test_connect_peer_called_on_spawn() -> void:
	var _player := harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	var scene := harness.get_server_scene()
	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(scene.synchronizer.connected_peers.has(peer_id)).is_true()


func test_two_players_in_same_scene() -> void:
	harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	harness.spawn_player(client1, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client1, &"TestLevel")

	var scene := harness.get_server_scene()

	var peer_id_0 := client0.multiplayer_peer.get_unique_id()
	var peer_id_1 := client1.multiplayer_peer.get_unique_id()
	assert_that(scene.synchronizer.connected_peers.has(peer_id_0)).is_true()
	assert_that(scene.synchronizer.connected_peers.has(peer_id_1)).is_true()
