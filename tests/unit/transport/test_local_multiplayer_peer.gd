## Unit tests for [LocalMultiplayerPeer] routing and signals.
class_name TestLocalMultiplayerPeer
extends NetwTestSuite

var server: LocalMultiplayerPeer
var client: LocalMultiplayerPeer


func before_test() -> void:
	server = auto_free(LocalMultiplayerPeer.new())
	client = auto_free(LocalMultiplayerPeer.new())
	server.create_server()
	client.create_client(42)
	server.force_connect_peer(42, client)
	client.force_connect_peer(1, server)


func test_peer_identity_and_connection_flow() -> void:
	assert_that(server._is_server()).is_true()
	assert_that(client._is_server()).is_false()
	assert_that(server._get_unique_id()).is_equal(1)
	assert_that(client._get_unique_id()).is_equal(42)
	assert_that(server._peers_to_emit_connected).contains([42])
	assert_that(client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTING,
	)

	client.poll()

	assert_that(client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)


func test_packet_routing_flow() -> void:
	client.poll()
	var payload := PackedByteArray([1, 2, 3])
	client._set_target_peer(1)
	client._put_packet_script(payload)

	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(payload)

	var client2: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	client2.create_client(99)
	server.force_connect_peer(99, client2)
	client2.force_connect_peer(1, server)

	client2.poll()

	server._set_target_peer(0)
	server._put_packet_script(PackedByteArray([7, 8]))

	assert_that(client._get_available_packet_count()).is_equal(1)
	assert_that(client2._get_available_packet_count()).is_equal(1)


func test_close_and_disconnect_flow() -> void:
	client.poll()

	var disconnected_ids: Array[int] = []
	server.peer_disconnected.connect(
		func(id: int) -> void: disconnected_ids.append(id)
	)

	client.close()
	client.poll()
	server.poll()

	assert_that(disconnected_ids).contains([42])

	server.force_connect_peer(42, client)
	client.force_connect_peer(1, server)
	client.create_client(42)
	client.poll()

	server.disconnect_peer(42)

	assert_that(server.linked_peers.has(42)).is_false()
	assert_that(client.linked_peers.has(1)).is_false()
	assert_that(client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_DISCONNECTED,
	)
