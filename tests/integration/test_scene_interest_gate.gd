## Integration test for [MultiplayerScene]'s gate-backed scene
## admission. Covers default-deny (pre-admission peers see no
## entities under the scene) and post-admission visibility, and
## asserts the client-side invariant that
## [code]layer.entities[/code] stays empty.
class_name TestSceneInterestGate
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE := preload("res://tests/helpers/test_player.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager
var server_scene: MultiplayerScene
var client0: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	server_mgr = harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	server_scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_scene_layer_id_matches_level_name() -> void:
	assert_that(String(server_scene.scene_layer_id())) \
			.is_equal("scene:TestLevel")


func test_default_deny_unadmitted_peer() -> void:
	# Before any add_viewer, no admitted peers on the gate.
	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(server_scene.connected_peers.has(peer_id)).is_false()
	assert_that(server_scene.scene_visibility_filter(peer_id)).is_false()


func test_admission_makes_peer_visible() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()
	server_scene.connect_peer(peer_id)
	assert_that(server_scene.connected_peers.has(peer_id)).is_true()
	assert_that(server_scene.scene_visibility_filter(peer_id)).is_true()


func test_client_layer_entities_empty_even_after_admission() -> void:
	# Spawn a player so the server's layer has entities, then verify
	# the client's mirror of that layer carries no entity membership
	# (entities are server-only state).
	harness.spawn_player(client0, TEST_PLAYER_SCENE)
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")

	# Server-side: layer has the player as an entity.
	var server_layer := server_scene.layer
	assert_that(server_layer.entities.is_empty()).is_false()

	# Client-side: same layer id resolves to a NetwInterestLayer
	# whose entities set is empty (client never receives entity
	# membership over the wire).
	var client_layer := client0.interest.layer(
			server_scene.scene_layer_id())
	assert_that(client_layer.entities.is_empty()).is_true()
