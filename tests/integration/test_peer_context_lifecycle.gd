## Integration tests for PeerContext lifecycle within a real multiplayer session.
##
## Covers two properties that have no unit-level equivalent:
##   1. Loopback isolation — server and client in the same process maintain
##      separate PeerContext instances and SaveComponent buckets never overlap.
##   2. Disconnect cleanup — the server erases a peer's context when that peer
##      disconnects, preventing stale state from leaking across sessions.
class_name TestPeerContextLifecycle
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SAVE_SCENE := preload("res://tests/helpers/TestLevelSave.tscn")
const SPAWNER_PATH := "TestPlayerWithSave/ClientComponent"
const LOBBY_NAME := &"TestLevelSave"

var harness: NetworkTestHarness
var client0: MultiplayerTree
var save_dir: String


func before_test() -> void:
	save_dir = create_temp_dir("peer_context_lifecycle")

	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)

	harness.get_server().lobby_manager.add_spawnable_scene(TEST_LEVEL_SAVE_SCENE.resource_path)

	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame


# ---------------------------------------------------------------------------
# Disconnect cleanup
# ---------------------------------------------------------------------------

func test_context_erased_on_peer_disconnect() -> void:
	var server := harness.get_server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	# Force creation of a context on the server keyed by the client's peer_id.
	# This simulates any future server-side code that stores per-client state.
	server.get_peer_context(client_peer_id)
	assert_that(server._peer_contexts.has(client_peer_id)).is_true()

	client0.multiplayer_peer.close()
	await wait_until(func(): return not server._peer_contexts.has(client_peer_id))

	assert_that(server._peer_contexts.has(client_peer_id)).is_false()


# ---------------------------------------------------------------------------
# Loopback isolation — SaveComponent.Bucket
# ---------------------------------------------------------------------------

func _spawn_save_player() -> void:
	var player: Node2D = await harness.join_player(
		client0, TEST_LEVEL_SAVE_SCENE.resource_path, SPAWNER_PATH)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.save_dir = save_dir

	# Wait for the replicated player to appear on the client side so both
	# buckets are populated before any assertions run.
	await harness.wait_for_client_player_spawn(client0, LOBBY_NAME)


func test_server_context_does_not_contain_client_peer_id() -> void:
	await _spawn_save_player()

	var server := harness.get_server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	# Client-side components register in the client's own MultiplayerTree.
	# The server should have no context keyed by the client's peer_id.
	assert_that(server._peer_contexts.has(client_peer_id)).is_false()


func test_client_context_does_not_contain_server_peer_id() -> void:
	await _spawn_save_player()

	# Server-side components register in the server's own MultiplayerTree.
	# The client should have no context keyed by the server's peer_id (1).
	assert_that(client0._peer_contexts.has(1)).is_false()


func test_save_buckets_contain_no_shared_component_instances() -> void:
	await _spawn_save_player()

	var server := harness.get_server()
	var client_peer_id := client0.multiplayer_peer.get_unique_id()

	var server_bucket := server \
		.get_peer_context(1) \
		.get_bucket(SaveComponent.Bucket) as SaveComponent.Bucket

	var client_bucket := client0 \
		.get_peer_context(client_peer_id) \
		.get_bucket(SaveComponent.Bucket) as SaveComponent.Bucket

	assert_that(server_bucket.registered).is_not_empty()
	assert_that(client_bucket.registered).is_not_empty()

	for comp in server_bucket.registered:
		assert_that(client_bucket.registered.has(comp)).is_false()
