class_name TestMultiClientConnection
extends GdUnitTestSuite

var harness: NetworkTestHarness


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	# No lobby manager — this suite only tests the connection layer.
	await harness.setup(2, null)


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame

	SaveComponent.registered_components.clear()
	TPComponent._pending.clear()


func test_server_is_online_after_connect() -> void:
	await harness.connect_all()
	assert_that(harness.get_server().is_online()).is_true()


func test_all_clients_online_after_connect() -> void:
	await harness.connect_all()
	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()


func test_clients_have_distinct_peer_ids() -> void:
	await harness.connect_all()
	var id0 := harness.get_client(0).multiplayer_peer.get_unique_id()
	var id1 := harness.get_client(1).multiplayer_peer.get_unique_id()
	assert_that(id0).is_not_equal(id1)


func test_client_peer_ids_are_not_server_id() -> void:
	await harness.connect_all()
	for client in harness.get_all_clients():
		assert_that(client.multiplayer_peer.get_unique_id()).is_not_equal(1)


func test_server_emits_peer_connected_for_each_client() -> void:
	var connected_ids: Array[int] = []
	harness.get_server().peer_connected.connect(func(id: int) -> void:
		connected_ids.append(id)
	)
	await harness.connect_all()
	await await_millis(200)
	assert_that(connected_ids.size()).is_equal(2)


func test_three_clients_all_online() -> void:
	# Re-setup with 3 clients
	harness.queue_free()
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(3, null)
	await harness.connect_all()

	for client in harness.get_all_clients():
		assert_that(client.is_online()).is_true()

	var ids: Array[int] = []
	for client in harness.get_all_clients():
		ids.append(client.multiplayer_peer.get_unique_id())
	# All IDs must be unique
	assert_that(ids.size()).is_equal(ids.filter(func(id): return ids.count(id) == 1).size())
