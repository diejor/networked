## Integration test for [MultiplayerPeer] connection and topology.
class_name TestMultiClientConnection
extends NetwTestSuite

var harness: NetwTestHarness


func before_test() -> void:
	harness = make_harness()
	await harness.setup()


func test_clients_connect_online_with_distinct_peer_ids() -> void:
	var connected_ids: Array[int] = []
	harness.server().api.peer_connected.connect(
		func(id: int) -> void:
			connected_ids.append(id)
	)

	await harness.add_client()
	await harness.add_client()
	await harness.add_client()

	assert_that(harness.server().api.is_online).is_true()
	assert_that(connected_ids.size()).is_equal(3)

	var ids: Array[int] = []
	for client in harness.clients():
		assert_that(client.api.is_online).is_true()
		var id := client.multiplayer_peer.get_unique_id()
		assert_that(id).is_not_equal(1)
		ids.append(id)

	assert_that(ids.size()).is_equal(
		ids.filter(func(id): return ids.count(id) == 1).size(),
	)
