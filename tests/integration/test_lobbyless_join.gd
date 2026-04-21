## Integration test for the lobby-less join flow.
## Verifies that MultiplayerTree emits player_join_requested when no LobbyManager is present.
class_name TestLobbylessJoin
extends NetworkedTestSuite

func test_lobbyless_join() -> void:
	# 1. Setup harness WITHOUT a LobbyManager scene
	var h: NetworkTestHarness = auto_free(NetworkTestHarness.new())
	add_child(h)
	await h.setup(null) # No lobby manager
	
	var server: MultiplayerTree = h.get_server()
	var client: MultiplayerTree = await h.add_client()
	
	# 2. Connect to the signal on the server
	var join_received := [false]
	var received_data: Array[MultiplayerClientData]= [null]
	server.player_join_requested.connect(func(data: MultiplayerClientData):
		join_received[0] = true
		received_data[0] = data
	)
	
	# 3. Request join from client
	var username: String = client.get_meta(&"_harness_username")
	var spawner_path := SceneNodePath.new()
	spawner_path.scene_path = "res://tests/helpers/TestLevel.tscn"
	spawner_path.node_path = "TestPlayerFull/ClientComponent"
	
	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.spawner_path = spawner_path
	
	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		client_data.serialize()
	)
	
	# 4. Wait for signal on server
	await wait_until(func(): return join_received[0])
		
	assert_that(join_received[0]).is_true()
	assert_that(str(received_data[0].username)).is_equal(username)
	assert_that(received_data[0].peer_id).is_equal(client.multiplayer_peer.get_unique_id())
