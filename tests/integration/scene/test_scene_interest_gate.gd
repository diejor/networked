## Integration test for [MultiplayerScene]'s gate-backed scene
## admission. Covers default-deny (pre-admission peers see no
## entities under the scene) and post-admission visibility, and
## asserts the client-side invariant that [code]layer.entities[/code]
## is populated by [method InterestGate.track_entity].
class_name TestSceneInterestGate
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var server_scene: MultiplayerScene
var client0: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D)
	player_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)
	server_mgr = harness.server_scene_manager()

	client0 = await harness.add_client()

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	server_scene = server_mgr.active_scenes.values()[0]


func test_scene_layer_defaults_to_deny_until_admission() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()

	assert_that(String(server_scene.scene_layer_id())) \
			.is_equal("scene:%s" % level_builder.scene_name)
	assert_that(server_scene.connected_peers.has(peer_id)).is_false()
	assert_that(server_scene.scene_visibility_filter(peer_id)).is_false()


func test_admission_makes_peer_visible_and_populates_client_layer() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.connect_peer(peer_id)
	assert_that(server_scene.connected_peers.has(peer_id)).is_true()
	assert_that(server_scene.scene_visibility_filter(peer_id)).is_true()

	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)

	var server_layer := server_scene.layer
	assert_that(server_layer.entities.is_empty()).is_false()

	var client_layer := client0.interest.layer(
		server_scene.scene_layer_id(),
	)
	assert_that(client_layer.entities.is_empty()).is_false()
