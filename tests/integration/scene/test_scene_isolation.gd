## Integration tests for scene peer isolation.
class_name TestLobbyIsolation
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var scene: NetwSceneHandle
var client0: MultiplayerTree
var client1: MultiplayerTree
var level_builder: LevelBuilder


func before_test() -> void:
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	harness.register_spawnable_scene(level_builder.packed)
	server_api = harness.server().api

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	assert_that(server_api.scene_instances().size()).is_equal(1)
	scene = server_api.scene_instances()[0]


func test_initial_visibility_and_connected_peers() -> void:
	var client_id := client0.multiplayer_peer.get_unique_id()
	var second_id := client1.multiplayer_peer.get_unique_id()

	assert_that(scene.peers).is_empty()
	assert_that(scene.admits(client_id)) \
			.is_false()
	assert_that(
		scene.admits(
			MultiplayerPeer.TARGET_PEER_SERVER,
		),
	).is_false()
	var scene_entity := scene.record
	assert_bool(
		harness.server().api._interest.wire_admits(
			MultiplayerPeer.TARGET_PEER_SERVER,
			scene_entity,
		),
	).is_true()
	assert_that(scene.admits(second_id)) \
			.is_false()


