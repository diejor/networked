## Integration test for the lobby-less join flow.
## Verifies that dropping a Level as a direct child of MultiplayerTree
## automatically handles join routing and player spawning.
class_name TestLobbylessJoin
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")

var harness: NetworkTestHarness
var client: MultiplayerTree


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(null, TEST_LEVEL_SCENE)
	client = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func test_lobbyless_lobby_created_on_server() -> void:
	var server := harness.get_server()
	var lobby := server.get_node_or_null("TestLevelLobby")
	assert_that(lobby).is_not_null()
	assert_that(lobby.get_meta(&"_is_lobbyless", false)).is_true()


func test_level_inside_lobby_on_server() -> void:
	var server := harness.get_server()
	var level := server.get_node_or_null("TestLevelLobby/TestLevel")
	assert_that(level).is_not_null()


func test_player_spawns_in_level_after_join() -> void:
	var server := harness.get_server()
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()

	var spawner_path := SceneNodePath.new()
	spawner_path.scene_path = "res://tests/helpers/TestLevel.tscn"
	spawner_path.node_path = "TestPlayerFull/SpawnerComponent"

	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.spawner_path = spawner_path

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		client_data.serialize()
	)

	var player_name := "%s|%d" % [username, peer_id]
	var level := server.get_node_or_null("TestLevelLobby/TestLevel")

	await wait_until(func(): return level != null and level.get_node_or_null(player_name) != null)

	assert_that(level.get_node_or_null(player_name)).is_not_null()


func test_spawned_player_has_correct_username() -> void:
	var server := harness.get_server()
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()

	var spawner_path := SceneNodePath.new()
	spawner_path.scene_path = "res://tests/helpers/TestLevel.tscn"
	spawner_path.node_path = "TestPlayerFull/SpawnerComponent"

	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.spawner_path = spawner_path

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		client_data.serialize()
	)

	var player_name := "%s|%d" % [username, peer_id]
	var level := server.get_node_or_null("TestLevelLobby/TestLevel")
	await wait_until(func(): return level != null and level.get_node_or_null(player_name) != null)

	var player := level.get_node(player_name)
	var client_comp := SpawnerComponent.unwrap(player)
	assert_that(client_comp).is_not_null()
	assert_that(str(client_comp.username)).is_equal(username)


func test_lobby_context_accessible_from_level_node() -> void:
	var server := harness.get_server()
	var level := server.get_node_or_null("TestLevelLobby/TestLevel")
	assert_that(level).is_not_null()

	var ctx := NetLobbyContext.for_node(level)
	assert_that(ctx).is_not_null()
	assert_that(ctx.is_valid()).is_true()
	assert_that(ctx.is_lobbyless()).is_true()
