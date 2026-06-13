## Integration tests for [MultiplayerPeer] connection and topology.
class_name TestMultiClientConnection
extends NetwTestSuite

var harness: NetwTestHarness


func before_test() -> void:
	harness = make_harness()
	# No lobby manager. This suite only tests the connection layer.
	# Tests add clients themselves so signal handlers can be connected first.
	await harness.setup()


func test_two_clients_connect_online_with_distinct_peer_ids() -> void:
	var client0 := await harness.add_client()
	var client1 := await harness.add_client()
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()

	assert_that(harness.server().is_online()).is_true()
	for client in harness.clients():
		assert_that(client.is_online()).is_true()
		assert_that(client.multiplayer_peer.get_unique_id()).is_not_equal(1)
	assert_that(id0).is_not_equal(id1)


func test_server_emits_peer_connected_for_each_client() -> void:
	var connected_ids: Array[int] = []
	harness.server().peer_connected.connect(
		func(id: int) -> void:
			connected_ids.append(id)
	)
	await harness.add_client()
	await harness.add_client()
	assert_that(connected_ids.size()).is_equal(2)


func test_three_clients_all_online() -> void:
	await harness.add_client()
	await harness.add_client()
	await harness.add_client()

	for client in harness.clients():
		assert_that(client.is_online()).is_true()

	var ids: Array[int] = []
	for client in harness.clients():
		ids.append(client.multiplayer_peer.get_unique_id())
	# All IDs must be unique
	assert_that(ids.size()).is_equal(
		ids.filter(func(id): return ids.count(id) == 1).size(),
	)
