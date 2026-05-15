## Integration tests for [MultiplayerScene] peer isolation, exercised via
## the new [NetwInterestLayer] API on [member MultiplayerScene.layer].
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

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


# ---------------------------------------------------------------------------
# Server-side layer membership is the source of truth.
# ---------------------------------------------------------------------------

func test_scene_layer_exists_after_spawn() -> void:
	assert_that(scene.layer).is_not_null()
	assert_that(scene.layer.policy).is_equal(NetwInterestLayer.Policy.ISOLATE)


func test_layer_empty_initially() -> void:
	assert_that(scene.layer.members()).is_empty()


func test_unregistered_peer_not_in_layer() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	assert_that(scene.layer.has_member(client_id)).is_false()


func test_connect_peer_adds_layer_member() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	assert_that(scene.layer.has_member(client_id)).is_true()


func test_second_peer_not_in_layer_when_only_first_connected() -> void:
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(id0)
	assert_that(scene.layer.has_member(id0)).is_true()
	assert_that(scene.layer.has_member(id1)).is_false()


func test_disconnect_peer_removes_layer_member() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	scene.synchronizer.disconnect_peer(client_id)
	assert_that(scene.layer.has_member(client_id)).is_false()


# ---------------------------------------------------------------------------
# Back-compat: connected_peers mirror stays in sync with layer.
# ---------------------------------------------------------------------------

func test_connected_peers_mirror_layer_state() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)
	assert_that(scene.synchronizer.connected_peers.has(client_id)).is_true()
	scene.synchronizer.disconnect_peer(client_id)
	await wait_until(
		func(): return not scene.synchronizer.connected_peers.has(client_id))
	assert_that(scene.synchronizer.connected_peers.has(client_id)).is_false()


# ---------------------------------------------------------------------------
# Client-side mirror: the layer shows up on the joined client.
# ---------------------------------------------------------------------------

func test_client_sees_scene_layer_mirror_after_join() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.synchronizer.connect_peer(client_id)

	await wait_until(
		func(): return client0.interest.layer(scene._layer_id()) != null)
	var mirror := client0.interest.layer(scene._layer_id())
	assert_that(mirror).is_not_null()
	assert_that(mirror._is_mirror).is_true()
	assert_that(mirror.has_member(client_id)).is_true()


func test_client_layer_signals_fire_on_membership_change() -> void:
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()

	# Connect client0 first so it observes the layer.
	scene.synchronizer.connect_peer(id0)
	await wait_until(
		func(): return client0.interest.layer(scene._layer_id()) != null)
	var mirror := client0.interest.layer(scene._layer_id())

	var added: Array[int] = []
	mirror.member_added.connect(func(p: int): added.append(p))

	scene.synchronizer.connect_peer(id1)
	await wait_until(func(): return mirror.has_member(id1))
	assert_that(added).contains([id1])
