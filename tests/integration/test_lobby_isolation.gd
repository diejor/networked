class_name TestLobbyIsolation
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerLobbyManager
var lobby: Lobby
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)

	server_mgr = harness._get_lobby_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	# spawn_lobbies() runs synchronously inside host(), so active_lobbies is
	# already populated by the time add_client() returns. No signal await needed.
	assert_that(server_mgr.active_lobbies.size()).is_equal(1)
	lobby = server_mgr.active_lobbies.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_connected_clients_empty_initially() -> void:
	assert_that(lobby.synchronizer.connected_clients).is_empty()


func test_unregistered_peer_not_visible() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	assert_that(lobby.synchronizer.scene_visibility_filter(client_id)).is_false()


func test_registered_peer_becomes_visible() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	lobby.synchronizer.connect_client(client_id)
	assert_that(lobby.synchronizer.scene_visibility_filter(client_id)).is_true()


func test_second_client_not_visible_when_first_registered() -> void:
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()
	lobby.synchronizer.connect_client(id0)
	assert_that(lobby.synchronizer.scene_visibility_filter(id1)).is_false()


func test_server_always_visible_regardless_of_registered_clients() -> void:
	assert_that(
		lobby.synchronizer.scene_visibility_filter(MultiplayerPeer.TARGET_PEER_SERVER)
	).is_true()


# --- connect_client / disconnect_client with real peers ---
# These tests call set_visibility_for() via the C++ MultiplayerSynchronizer
# bridge and must run here where peers are actually registered in the
# engine's replication interface. Calling them without a real connection
# produces ERR_INVALID_PARAMETER from _update_sync_visibility().

func test_disconnect_client_adds_to_connected_clients() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	lobby.synchronizer.connect_client(client_id)
	assert_that(lobby.synchronizer.connected_clients.has(client_id)).is_true()


func test_disconnect_client_removes_from_connected_clients() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	lobby.synchronizer.connect_client(client_id)
	lobby.synchronizer.disconnect_client(client_id)
	await wait_until(func(): return not lobby.synchronizer.connected_clients.has(client_id))
	assert_that(lobby.synchronizer.connected_clients.has(client_id)).is_false()


func test_disconnect_client_removes_visibility() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	lobby.synchronizer.connect_client(client_id)
	lobby.synchronizer.disconnect_client(client_id)
	await wait_until(func(): return not lobby.synchronizer.scene_visibility_filter(client_id))
	assert_that(lobby.synchronizer.scene_visibility_filter(client_id)).is_false()
