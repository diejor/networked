## Integration tests for MultiplayerTree.connect_player().
##
## These cover the production entry-point addon users call directly.
## Setup is intentionally explicit (no NetworkTestHarness) so the test doubles
## as documentation for what MultiplayerTree requires to function.
##
## The server is a plain MultiplayerTree — matching the production dedicated-server
## model — while the client side goes through MultiplayerTree.connect_player().
class_name TestMultiplayerTreeConnect
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

## Path from the level root to the [SpawnerPlayerComponent] that acts as the spawn template.
const SPAWNER_PATH := "TestPlayerFull/SpawnerPlayerComponent"

var session: LocalLoopbackSession
var server: MultiplayerTree
var client_tree: MultiplayerTree


func before_test() -> void:
	session = LocalLoopbackSession.new()

	_setup_server()
	_setup_client()

	# One frame so _ready() fires on all added nodes before host() runs.
	await get_tree().process_frame
	server.host()


func after_test() -> void:
	session = null


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

func test_client_is_online_after_connect_player() -> void:
	client_tree.connect_player(_create_join_payload("alice"))
	await timeout_await(client_tree.connected_to_server)
	assert_that(client_tree.is_online()).is_true()


func test_listen_server_connect_player_spawns_player() -> void:
	var tree := MultiplayerTree.new()
	tree.name = "ListenServer"
	tree.use_listen_server = true
	tree.auto_host_headless = false
	add_child(tree)
	auto_free(tree)

	var ls_session := LocalLoopbackSession.new()
	var backend := LocalLoopbackBackend.new()
	backend.session = ls_session
	tree.backend = backend

	var mgr: MultiplayerSceneManager = (
		NetworkedTestSuite.create_scene_manager()
	)
	tree.add_child(mgr)
	mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	var err := await tree.connect_player(_create_join_payload("alice"))
	assert_that(err).is_equal(OK)
	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)

	var scene := mgr.active_scenes.values()[0] as MultiplayerScene
	assert_that(scene).is_not_null()

	var timeout_timer := get_tree().create_timer(1.0)
	while scene.level.get_node_or_null("alice|1") == null:
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			assert(false, "Timed out waiting for player 'alice|1' to spawn")
			return

	assert_that(scene.level.get_node_or_null("alice|1")).is_not_null()


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

func _setup_server() -> void:
	server = MultiplayerTree.new()
	server.name = "Server"
	server.is_server = true
	server.auto_host_headless = false
	add_child(server)
	auto_free(server)

	var backend := LocalLoopbackBackend.new()
	backend.session = session
	server.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	server.add_child(mgr)
	# Scenes must be registered before host() because spawn_lobbies() runs
	# synchronously inside _on_configured(), which fires during host().
	mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)


func _setup_client() -> void:
	client_tree = MultiplayerTree.new()
	client_tree.name = "ClientTree"
	add_child(client_tree)
	auto_free(client_tree)

	var backend := LocalLoopbackBackend.new()
	backend.session = session
	client_tree.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	client_tree.add_child(mgr)


func _create_join_payload(username: String) -> JoinPayload:
	var spawner_component_path := SceneNodePath.new()
	spawner_component_path.scene_path = TEST_LEVEL_SCENE.resource_path
	spawner_component_path.node_path = SPAWNER_PATH

	var data := JoinPayload.new()
	data.username = username
	data.url = "localhost"
	data.spawner_component_path = spawner_component_path
	return data
