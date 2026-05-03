class_name TestLobbyIsolation
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager
var scene: MultiplayerScene
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	server_mgr = harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	# spawn_scenes() runs synchronously inside host(), so active_scenes is
	# already populated by the time add_client() returns. No signal await needed.
	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_connected_peers_empty_initially() -> void:
	assert_that(scene.synchronizer.connected_peers).is_empty()


func test_unregistered_peer_not_visible() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	assert_that(scene.synchronizer.scene_visibility_filter(client_id)).is_false()


func test_registered_peer_becomes_visible() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	assert_that(scene.synchronizer.scene_visibility_filter(client_id)).is_true()


func test_second_peer_not_visible_when_first_registered() -> void:
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(id0)
	assert_that(scene.synchronizer.scene_visibility_filter(id1)).is_false()


func test_server_always_visible_regardless_of_registered_peers() -> void:
	assert_that(
		scene.synchronizer.scene_visibility_filter(MultiplayerPeer.TARGET_PEER_SERVER)
	).is_true()


# --- connect_peer / disconnect_peer with real peers ---
# These tests call set_visibility_for() via the C++ MultiplayerSynchronizer
# bridge and must run here where peers are actually registered in the
# engine's replication interface. Calling them without a real connection
# produces ERR_INVALID_PARAMETER from _update_sync_visibility().

func test_disconnect_peer_adds_to_connected_peers() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	assert_that(scene.synchronizer.connected_peers.has(client_id)).is_true()


func test_disconnect_peer_removes_from_connected_peers() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	scene.synchronizer.disconnect_peer(client_id)
	await wait_until(func(): return not scene.synchronizer.connected_peers.has(client_id))
	assert_that(scene.synchronizer.connected_peers.has(client_id)).is_false()


func test_disconnect_peer_removes_visibility() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	scene.synchronizer.disconnect_peer(client_id)
	await wait_until(func(): return not scene.synchronizer.scene_visibility_filter(client_id))
	assert_that(scene.synchronizer.scene_visibility_filter(client_id)).is_false()
