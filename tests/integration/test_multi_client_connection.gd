class_name TestMultiClientConnection
extends NetworkedTestSuite

var harness: NetworkTestHarness


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	# No lobby manager — this suite only tests the connection layer.
	# Tests add clients themselves so signal handlers can be connected first.
	await harness.setup(null)


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame


func test_server_is_online_after_connect() -> void:
	await harness.add_client()
	await harness.add_client()
	assert_that(harness.get_server().is_online()).is_true()


func test_all_clients_online_after_connect() -> void:
	await harness.add_client()
	await harness.add_client()
	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()


func test_clients_have_distinct_peer_ids() -> void:
	var client0 := await harness.add_client()
	var client1 := await harness.add_client()
	var id0 := client0.multiplayer_peer.get_unique_id()
	var id1 := client1.multiplayer_peer.get_unique_id()
	assert_that(id0).is_not_equal(id1)


func test_client_peer_ids_are_not_server_id() -> void:
	await harness.add_client()
	await harness.add_client()
	for client in harness.get_all_clients():
		assert_that(client.multiplayer_peer.get_unique_id()).is_not_equal(1)


func test_server_emits_peer_connected_for_each_client() -> void:
	var connected_ids: Array[int] = []
	harness.get_server().peer_connected.connect(func(id: int) -> void:
		connected_ids.append(id)
	)
	await harness.add_client()
	await harness.add_client()
	assert_that(connected_ids.size()).is_equal(2)


func test_three_clients_all_online() -> void:
	harness.queue_free()
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(null)
	await harness.add_client()
	await harness.add_client()
	await harness.add_client()

	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()

	var ids: Array[int] = []
	for client in harness.get_all_clients():
		ids.append(client.multiplayer_peer.get_unique_id())
	# All IDs must be unique
	assert_that(ids.size()).is_equal(ids.filter(func(id): return ids.count(id) == 1).size())
