## Integration tests for [MultiplayerScene] peer isolation.
class_name TestLobbyIsolation
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var scene: MultiplayerScene
var client0: MultiplayerTree
var client1: MultiplayerTree
var level_builder: LevelBuilder


func before_test() -> void:
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	harness.register_spawnable_scene(level_builder.packed)
	server_mgr = harness.server_scene_manager()

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_initial_visibility_and_connected_peers() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	var second_id := client1.multiplayer_peer.get_unique_id()

	assert_that(scene.connected_peers).is_empty()
	assert_that(scene.scene_visibility_filter(client_id)) \
			.is_false()
	assert_that(
		scene.scene_visibility_filter(
			MultiplayerPeer.TARGET_PEER_SERVER,
		),
	).is_true()
	assert_that(scene.scene_visibility_filter(second_id)) \
			.is_false()


func test_connect_peer_adds_visibility_and_connected_peer() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	var second_id := client1.multiplayer_peer.get_unique_id()

	scene.connect_peer(client_id)

	assert_that(scene.connected_peers.has(client_id)).is_true()
	assert_that(scene.scene_visibility_filter(client_id)) \
			.is_true()
	assert_that(scene.scene_visibility_filter(second_id)) \
			.is_false()


func test_disconnect_peer_removes_connected_peer_and_visibility() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	scene.connect_peer(client_id)

	scene.disconnect_peer(client_id)

	await wait_until(
		func(): return not scene.connected_peers.has(client_id)
	)
	assert_that(scene.connected_peers.has(client_id)).is_false()
	@warning_ignore("redundant_await")
	await assert_func(scene, "scene_visibility_filter", [client_id]) \
			.wait_until(1000) \
			.is_false()
