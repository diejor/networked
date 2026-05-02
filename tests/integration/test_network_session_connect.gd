## Integration tests for NetworkSession.connect_player().
##
## These cover the production entry-point addon users call directly.
## Setup is intentionally explicit (no NetworkTestHarness) so the test doubles
## as documentation for what NetworkSession requires to function.
##
## The server is a plain MultiplayerTree — matching the production dedicated-server
## model — while the client side goes through NetworkSession.connect_player()
## with manage_scene = false so scene management is skipped.
class_name TestNetworkSessionConnect
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

## Path from the level root to the [SpawnerComponent] that acts as the spawn template.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var session: LocalLoopbackSession
var server: MultiplayerTree
var network: NetworkSession


func before_test() -> void:
	session = LocalLoopbackSession.new()

	_setup_server()
	_setup_network()

	# One frame so _ready() fires on all added nodes before host() runs.
	await get_tree().process_frame
	server.host()


func after_test() -> void:
	session = null


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

func test_client_is_online_after_connect_player() -> void:
	network.connect_player(_client_data("alice"))
	await timeout_await(network.client.connected_to_server)
	assert_that(network.client.is_online()).is_true()

#
#func test_client_peer_id_is_not_server_id() -> void:
	#network.connect_player(_client_data("alice"))
	#await timeout_await(network.client.connected_to_server)
	#assert_that(network.client.multiplayer_peer.get_unique_id()).is_not_equal(1)
#
#
#func test_server_registers_peer_after_connect() -> void:
	#network.connect_player(_client_data("alice"))
	#await timeout_await(network.client.connected_to_server)
	#assert_that(server.multiplayer_api.get_peers().size()).is_equal(1)
#
#
#func test_server_emits_peer_connected_on_connect() -> void:
	#var connected_ids: Array[int] = []
	#server.peer_connected.connect(func(id: int) -> void: connected_ids.append(id))
#
	#network.connect_player(_client_data("alice"))
	#await timeout_await(network.client.connected_to_server)
#
	#assert_that(connected_ids.size()).is_equal(1)
#
#
#func test_player_spawns_in_server_scene_after_connect() -> void:
	#network.connect_player(_client_data("alice"))
	#await timeout_await(network.client.connected_to_server)
	#
	#var peer_id := network.client.multiplayer_api.get_unique_id()
	#var sm: MultiplayerSceneManager = server.get_service(MultiplayerSceneManager)
	#await wait_until(func():
		#@warning_ignore("confusable_local_declaration")
		#var scene: MultiplayerScene = sm.active_scenes.get(&"TestLevel")
		#return scene and scene.level.get_node_or_null("alice|%d" % peer_id) != null
	#, 5.0)
#
	#var scene: MultiplayerScene = sm.active_scenes.get(&"TestLevel")
	#var player := scene.level.get_node_or_null("alice|%d" % peer_id)
	#assert_that(player).is_not_null()
#
#
#func test_spawned_player_has_correct_multiplayer_authority() -> void:
	#network.connect_player(_client_data("alice"))
	#await timeout_await(network.client.connected_to_server)
	#
	#var peer_id := network.client.multiplayer_api.get_unique_id()
	#var sm: MultiplayerSceneManager = server.get_service(MultiplayerSceneManager)
	#await wait_until(func():
		#@warning_ignore("confusable_local_declaration")
		#var scene: MultiplayerScene = sm.active_scenes.get(&"TestLevel")
		#return scene and scene.level.get_node_or_null("alice|%d" % peer_id) != null
	#, 5.0)
#
	#var scene: MultiplayerScene = sm.active_scenes.get(&"TestLevel")
	#var player := scene.level.get_node_or_null("alice|%d" % peer_id)
	#assert_that(player.get_multiplayer_authority()).is_equal(peer_id)


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

func _setup_server() -> void:
	server = MultiplayerTree.new()
	server.name = "Server"
	server.is_server = true
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


func _setup_network() -> void:
	network = NetworkSession.new()
	network.manage_scene = false
	auto_free(network)

	var client_tree := MultiplayerTree.new()
	client_tree.name = "ClientTree"
	add_child(client_tree)
	auto_free(client_tree)

	var backend := LocalLoopbackBackend.new()
	backend.session = session
	client_tree.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	client_tree.add_child(mgr)

	# Assigning client triggers signal wiring inside NetworkSession.
	network.client = client_tree
	add_child(network)


func _client_data(username: String) -> MultiplayerClientData:
	var spawner_component_path := SceneNodePath.new()
	spawner_component_path.scene_path = TEST_LEVEL_SCENE.resource_path
	spawner_component_path.node_path = SPAWNER_PATH

	var data := MultiplayerClientData.new()
	data.username = username
	data.url = "localhost"
	data.spawner_component_path = spawner_component_path
	return data
