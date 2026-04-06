class_name TestLocalMultiplayerPeer
extends GdUnitTestSuite

var server: LocalMultiplayerPeer
var client: LocalMultiplayerPeer


func before_test() -> void:
	server = auto_free(LocalMultiplayerPeer.new())
	client = auto_free(LocalMultiplayerPeer.new())
	server.create_server()
	client.create_client(42)
	server.force_connect_peer(42, client)
	client.force_connect_peer(1, server)


func test_server_reports_is_server() -> void:
	assert_that(server._is_server()).is_true()


func test_client_reports_not_server() -> void:
	assert_that(client._is_server()).is_false()


func test_server_unique_id_is_one() -> void:
	assert_that(server._get_unique_id()).is_equal(1)


func test_client_unique_id_is_assigned() -> void:
	assert_that(client._get_unique_id()).is_equal(42)


func test_server_queues_peer_connected_on_force_connect() -> void:
	assert_that(server._peers_to_emit_connected).contains([42])


func test_client_starts_as_connecting() -> void:
	assert_that(client._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTING)


func test_client_becomes_connected_after_poll() -> void:
	client.poll()
	assert_that(client._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTED)


func test_packet_routed_from_client_to_server() -> void:
	client.poll()
	var payload := PackedByteArray([1, 2, 3])
	client._set_target_peer(1)
	client._put_packet_script(payload)

	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(payload)


func test_broadcast_from_server_reaches_all_clients() -> void:
	var client2: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	client2.create_client(99)
	server.force_connect_peer(99, client2)
	client2.force_connect_peer(1, server)

	client.poll()
	client2.poll()

	server._set_target_peer(0)
	server._put_packet_script(PackedByteArray([7, 8]))

	assert_that(client._get_available_packet_count()).is_equal(1)
	assert_that(client2._get_available_packet_count()).is_equal(1)


func test_close_emits_peer_disconnected_on_server() -> void:
	client.poll()  # finalize connection first

	var disconnected_ids: Array[int] = []
	server.peer_disconnected.connect(func(id: int) -> void: disconnected_ids.append(id))

	client.close()  # notifies server synchronously via _remote_closed
	client.poll()   # finalizes client close state
	server.poll()   # drains queue → emits peer_disconnected(42)

	assert_that(disconnected_ids).contains([42])
