class_name TestLocalLoopbackSession
extends NetworkedTestSuite

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
	assert_that(c1._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTING)
	assert_that(c2._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTING)
	session.poll()
	assert_that(c1._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTED)
	assert_that(c2._get_connection_status()).is_equal(MultiplayerPeer.CONNECTION_CONNECTED)


func test_reset_clears_server_and_all_clients() -> void:
	session.create_client_peer()
	session.create_client_peer()
	session.reset()
	assert_that(session.server_peer).is_null()
	assert_that(session.client_peers).is_empty()


func test_session_is_independent_of_shared_singleton() -> void:
	# GDUnit4 compares Resources by content, not identity — use instance ID instead
	assert_that(session.get_instance_id()).is_not_equal(
		LocalLoopbackSession.get_shared_session().get_instance_id()
	)
	# Clean up pollution for test isolation
	LocalLoopbackSession.shared = null


func test_get_client_peer_delegates_to_create() -> void:
	# get_client_peer() is the backward-compat alias — each call creates a new peer
	var c1 := session.get_client_peer()
	var c2 := session.get_client_peer()
	assert_that(c1).is_not_equal(c2)
	assert_that(session.client_peers.size()).is_equal(2)
