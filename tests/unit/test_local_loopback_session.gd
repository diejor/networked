## Unit tests for [LocalLoopbackSession] and peer lifecycle.
class_name TestLocalLoopbackSession
extends NetwTestSuite

var session: LocalLoopbackSession


func before_test() -> void:
	session = auto_free(LocalLoopbackSession.new())


func test_get_server_peer_returns_server() -> void:
	var srv := session.get_server_peer()
	assert_that(srv).is_not_null()
	assert_that(srv._is_server()).is_true()


func test_create_two_independent_client_peers() -> void:
	var c1 := session.create_client_peer()
	var c2 := session.create_client_peer()
	assert_that(c1).is_not_null()
	assert_that(c2).is_not_null()
	assert_that(c1).is_not_equal(c2)
	assert_that(c1._get_unique_id()).is_not_equal(c2._get_unique_id())


func test_both_clients_tracked_in_client_peers() -> void:
	var c1 := session.create_client_peer()
	var c2 := session.create_client_peer()
	assert_that(session.client_peers).contains([c1, c2])


func test_both_clients_linked_to_server() -> void:
	var c1 := session.create_client_peer()
	var c2 := session.create_client_peer()
	var srv := session.server_peer
	assert_that(srv.linked_peers.has(c1._get_unique_id())).is_true()
	assert_that(srv.linked_peers.has(c2._get_unique_id())).is_true()


func test_poll_advances_all_clients_to_connected() -> void:
	var c1 := session.create_client_peer()
	var c2 := session.create_client_peer()
	assert_that(c1._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTING,
	)
	assert_that(c2._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTING,
	)
	session.poll()
	assert_that(c1._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)
	assert_that(c2._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)


func test_client_close_notifies_server() -> void:
	var server := session.get_server_peer()
	var client := session.create_client_peer()
	var disconnected: Array[int] = []
	server.peer_disconnected.connect(
		func(peer_id: int):
			disconnected.append(peer_id)
	)
	session.poll()

	var client_id := client._get_unique_id()
	client.close()
	session.poll()

	assert_that(server.linked_peers.has(client_id)).is_false()
	assert_that(disconnected).contains_exactly([client_id])


func test_closed_client_does_not_block_new_client() -> void:
	var first_client := session.create_client_peer()
	session.poll()
	first_client.close()
	session.poll()

	var second_client := session.create_client_peer()
	session.poll()

	assert_that(second_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)
	assert_that(
		session.server_peer.linked_peers.has(
			second_client._get_unique_id(),
		),
	).is_true()


func test_reset_clears_server_and_all_clients() -> void:
	session.create_client_peer()
	session.create_client_peer()
	session.server_app_id = &"test-app"
	session.reset()
	assert_that(session.server_peer).is_null()
	assert_that(session.server_app_id).is_equal(&"")
	assert_that(session.client_peers).is_empty()


func test_session_is_independent_of_shared_singleton() -> void:
	# GDUnit4 compares Resources by content, not identity - use instance ID
	assert_that(session.get_instance_id()).is_not_equal(
		LocalLoopbackSession.get_shared_session().get_instance_id(),
	)
	# Clean up pollution for test isolation
	LocalLoopbackSession.shared = null


func test_get_client_peer_delegates_to_create() -> void:
	# get_client_peer() is backward-compat - each call creates a new peer
	var c1 := session.get_client_peer()
	var c2 := session.get_client_peer()
	assert_that(c1).is_not_equal(c2)
	assert_that(session.client_peers.size()).is_equal(2)


func test_backend_query_reports_live_server() -> void:
	session.get_server_peer()
	session.create_client_peer()
	session.server_app_id = &"test-app"
	var backend := LocalLoopbackBackend.new()
	backend.session = session

	@warning_ignore("redundant_await")
	var result: ServerInfoResult = await backend.query_server_info("")

	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_that(result.info.is_local_listener).is_true()
	assert_that(result.info.players).is_equal(1)
	assert_that(result.info.app_id).is_equal(&"test-app")


func test_backend_query_without_live_server_is_unsupported() -> void:
	var backend := LocalLoopbackBackend.new()
	backend.session = session

	@warning_ignore("redundant_await")
	var result: ServerInfoResult = await backend.query_server_info("")

	assert_int(result.status).is_equal(ServerInfoResult.Status.UNSUPPORTED)
