## Unit tests for [LocalLoopbackSession] and peer lifecycle.
class_name TestLocalLoopbackSession
extends NetwTestSuite

var session: LocalLoopbackSession
var _period: float = 1000.0 / float(Engine.get_physics_ticks_per_second())


func before_test() -> void:
	session = auto_free(LocalLoopbackSession.new())


func after_test() -> void:
	super.after_test()
	if session:
		session.reset()
		session = null


func test_peer_lifecycle_flow() -> void:
	var server := session.get_server_peer()
	var first_client := session.create_client_peer()
	var second_client := session.create_client_peer()
	assert_that(server).is_not_null()
	assert_that(server._is_server()).is_true()
	assert_that(first_client).is_not_equal(second_client)
	assert_that(first_client._get_unique_id()).is_not_equal(
		second_client._get_unique_id(),
	)
	assert_that(session.client_peers).contains([first_client, second_client])
	assert_that(server.linked_peers.has(first_client._get_unique_id())).is_true()
	assert_that(server.linked_peers.has(second_client._get_unique_id())).is_true()
	assert_that(first_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTING,
	)
	assert_that(second_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTING,
	)

	session.poll()
	assert_that(first_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)
	assert_that(second_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)

	var disconnected: Array[int] = []
	server.peer_disconnected.connect(
		func(peer_id: int):
			disconnected.append(peer_id)
	)
	var first_client_id := first_client._get_unique_id()
	first_client.close()
	session.poll()
	assert_that(server.linked_peers.has(first_client_id)).is_false()
	assert_that(disconnected).contains_exactly([first_client_id])

	var third_client := session.create_client_peer()
	session.poll()
	assert_that(third_client._get_connection_status()).is_equal(
		MultiplayerPeer.CONNECTION_CONNECTED,
	)
	assert_that(server.linked_peers.has(third_client._get_unique_id())).is_true()

	var compat_client := session.get_client_peer()
	assert_that(compat_client).is_not_null()
	assert_that(session.client_peers.size()).is_equal(4)

	third_client.close()
	assert_that(third_client.loopback_session).is_null()

	session.server_app_id = &"test-app"
	session.reset()
	assert_that(session.server_peer).is_null()
	assert_that(session.server_app_id).is_equal(&"")
	assert_that(session.client_peers).is_empty()
	assert_that(server.loopback_session).is_null()
	assert_that(first_client.loopback_session).is_null()
	assert_that(second_client.loopback_session).is_null()
	assert_that(compat_client.loopback_session).is_null()

	var shared := LocalLoopbackSession.get_shared_session()
	assert_that(session.get_instance_id()).is_not_equal(
		shared.get_instance_id(),
	)
	LocalLoopbackSession.shared = null


func test_backend_probe_flow() -> void:
	var backend := LocalLoopbackBackend.new()
	backend.session = session

	@warning_ignore("redundant_await")
	var missing: BackendPeer.ProbeResult = await backend.probe_server_info("")
	assert_int(missing.status).is_equal(
		BackendPeer.ProbeResult.Status.UNSUPPORTED,
	)

	session.get_server_peer()
	session.create_client_peer()
	session.server_app_id = &"test-app"

	@warning_ignore("redundant_await")
	var live: BackendPeer.ProbeResult = await backend.probe_server_info("")
	assert_int(live.status).is_equal(BackendPeer.ProbeResult.Status.OK)
	assert_that(live.info.is_local_listener).is_true()
	assert_that(live.info.players).is_equal(1)
	assert_that(live.info.app_id).is_equal(&"test-app")


func test_delay_flow() -> void:
	var pair := _make_connected_pair()
	var server: LocalMultiplayerPeer = pair[0]
	var client: LocalMultiplayerPeer = pair[1]
	_install_latency(server, 3.0)

	var payload := PackedByteArray([1, 2, 3])
	client._set_target_peer(1)
	client._put_packet_script(payload)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(payload)

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	_install_latency(server, 3.0)
	client._set_target_peer(1)
	client._put_packet_script(payload)
	session.poll_frame_scoped()
	session.poll_frame_scoped()
	session.poll_frame_scoped()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(payload)

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	var queued_payload := PackedByteArray([9, 8, 7])
	client._set_target_peer(1)
	client._put_packet_script(queued_payload)
	assert_that(server._get_available_packet_count()).is_equal(1)
	_install_latency(server, 3.0)
	assert_that(server._get_available_packet_count()).is_equal(0)
	_poll_session(3)
	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(queued_payload)


func test_loss_and_ordering_flow() -> void:
	var pair := _make_connected_pair()
	var server: LocalMultiplayerPeer = pair[0]
	var client: LocalMultiplayerPeer = pair[1]
	var conditions := LocalLoopbackSession.LinkConditions.new(10)
	conditions.packet_loss = 1.0
	conditions.retransmit_ms = 3.0 * _period
	session.set_link_conditions(server, conditions)

	var payload := PackedByteArray([4, 5, 6])
	client._set_target_peer(1)
	client._put_packet_script(payload)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(1)
	assert_that(server._get_packet_script()).is_equal(payload)

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	conditions = LocalLoopbackSession.LinkConditions.new(10)
	conditions.packet_loss = 1.0
	session.set_link_conditions(server, conditions)
	client._set_target_peer(1)
	client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
	client._put_packet_script(PackedByteArray([7, 8, 9]))
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	conditions = LocalLoopbackSession.LinkConditions.new(123)
	conditions.jitter_ms = 6.0 * _period
	conditions.reorder = 1.0
	session.set_link_conditions(server, conditions)
	client._set_target_peer(1)
	for value in range(12):
		client._put_packet_script(PackedByteArray([value]))
		session.poll()
	_poll_session(12)
	assert_that(_drain_packet_values(server)).is_equal(range(12))

	var first_order := _run_unreliable_reorder(120)
	var second_order := _run_unreliable_reorder(120)
	var different_seed_order := _run_unreliable_reorder(121)
	assert_that(second_order).is_equal(first_order)
	assert_that(different_seed_order).is_not_equal(first_order)

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	conditions = LocalLoopbackSession.LinkConditions.new(10)
	conditions.duplicate = 1.0
	session.set_link_conditions(server, conditions)
	client._set_target_peer(1)
	client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
	client._put_packet_script(PackedByteArray([7]))
	session.poll()
	session.poll()
	assert_that(_drain_packet_values(server)).is_equal([7, 7])

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	conditions = LocalLoopbackSession.LinkConditions.new(10)
	conditions.throttle = 1.0
	conditions.throttle_ms = 4.0 * _period
	session.set_link_conditions(server, conditions)
	client._set_target_peer(1)
	client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
	for value in range(3):
		client._put_packet_script(PackedByteArray([value]))
		session.poll()
		assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(_drain_packet_values(server)).is_equal([0, 1, 2])

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	client = pair[1]
	conditions = LocalLoopbackSession.LinkConditions.new(120)
	conditions.jitter_ms = 4.0 * _period
	conditions.duplicate = 0.5
	session.set_link_conditions(server, conditions)
	client._set_target_peer(1)
	client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
	client._put_packet_script(PackedByteArray([1]))
	session.poll()
	var state = session._links_by_peer[server]
	var sender_id := client._get_unique_id()
	assert_that(state.rng_by_stream.keys()).contains(
		["%d:jitter" % sender_id],
	)
	assert_that(state.rng_by_stream.keys()).contains(
		["%d:duplicate" % sender_id],
	)


func test_sender_condition_flow() -> void:
	var pair := _make_connected_pair()
	var server: LocalMultiplayerPeer = pair[0]
	var first_client: LocalMultiplayerPeer = pair[1]
	var second_client := session.create_client_peer()
	session.poll()
	_install_latency(server, 3.0)
	first_client._set_target_peer(1)
	first_client._put_packet_script(PackedByteArray([1]))
	second_client._set_target_peer(1)
	second_client._put_packet_script(PackedByteArray([2]))
	session.purge_packets_from(first_client._get_unique_id())
	_poll_session(3)
	assert_that(_drain_packet_values(server)).is_equal([2])

	_fresh_session()
	server = session.get_server_peer()
	var delayed_client := session.create_client_peer()
	var immediate_client := session.create_client_peer()
	session.poll()
	var conditions := LocalLoopbackSession.LinkConditions.new(10)
	conditions.latency_ms = 3.0 * _period
	session.set_link_conditions(
		server,
		conditions,
		delayed_client._get_unique_id(),
	)
	delayed_client._set_target_peer(1)
	immediate_client._set_target_peer(1)
	delayed_client._put_packet_script(PackedByteArray([1]))
	immediate_client._put_packet_script(PackedByteArray([2]))
	session.poll()
	assert_that(_drain_packet_values(server)).is_equal([2])
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(_drain_packet_values(server)).is_equal([1])

	_fresh_session()
	server = session.get_server_peer()
	first_client = session.create_client_peer()
	second_client = session.create_client_peer()
	session.poll()
	_install_latency(server, 2.0)
	first_client._set_target_peer(1)
	second_client._set_target_peer(1)
	first_client._put_packet_script(PackedByteArray([1]))
	second_client._put_packet_script(PackedByteArray([2]))
	session.poll()
	assert_that(server._get_available_packet_count()).is_equal(0)
	session.poll()
	assert_that(_drain_packet_values(server)).is_equal([1, 2])

	_fresh_session()
	pair = _make_connected_pair()
	server = pair[0]
	var client: LocalMultiplayerPeer = pair[1]
	session.hold_inbound_packets(server)
	client._set_target_peer(1)
	client._put_packet_script(PackedByteArray([1]))
	session.poll()
	client._put_packet_script(PackedByteArray([2]))
	session.release_inbound_packets(server)
	assert_that(server._get_available_packet_count()).is_equal(2)
	assert_that(server._get_packet_script()).is_equal(PackedByteArray([1]))
	assert_that(server._get_packet_script()).is_equal(PackedByteArray([2]))


func test_clear_link_conditions_flushes_predictably() -> void:
	var pair := _make_connected_pair()
	var server: LocalMultiplayerPeer = pair[0]
	var client: LocalMultiplayerPeer = pair[1]

	var conditions := LocalLoopbackSession.LinkConditions.new(5)
	conditions.latency_ms = 11.0 * _period
	conditions.jitter_ms = 4.0 * _period
	conditions.reorder = 1.0
	session.set_link_conditions(server, conditions)

	client._set_target_peer(1)
	client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
	for value in range(8):
		client._put_packet_script(PackedByteArray([value]))

	session.poll()
	session.clear_link_conditions(server)

	assert_that(_drain_packet_values(server)).is_equal(
		[7, 4, 5, 3, 1, 6, 2, 0],
	)


func _fresh_session() -> void:
	if session:
		session.reset()
	session = auto_free(LocalLoopbackSession.new())


func _make_connected_pair() -> Array:
	var server := session.get_server_peer()
	var client := session.create_client_peer()
	session.poll()
	return [server, client]


func _install_latency(peer: LocalMultiplayerPeer, periods: float) -> void:
	var conditions := LocalLoopbackSession.LinkConditions.new(10)
	conditions.latency_ms = periods * _period
	session.set_link_conditions(peer, conditions)


func _run_unreliable_reorder(_seed: int, include_duplicates: bool = false) -> Array:
	_fresh_session()
	var pair := _make_connected_pair()
	var server: LocalMultiplayerPeer = pair[0]
	var client: LocalMultiplayerPeer = pair[1]

	var conditions := LocalLoopbackSession.LinkConditions.new(_seed)
	conditions.jitter_ms = 4.0 * _period
	conditions.reorder = 1.0
	conditions.duplicate = 0.5 if include_duplicates else 0.0
	session.set_link_conditions(server, conditions)

	client._set_target_peer(1)
	for value in range(8):
		client._set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_UNRELIABLE)
		client._put_packet_script(PackedByteArray([value]))
		session.poll()

	_poll_session(10)
	return _first_occurrences(_drain_packet_values(server))


func _poll_session(count: int) -> void:
	for _i in range(count):
		session.poll()


func _drain_packet_values(peer: LocalMultiplayerPeer) -> Array:
	var values := []
	while peer._get_available_packet_count() > 0:
		values.append(peer._get_packet_script()[0])
	return values


func _first_occurrences(values: Array) -> Array:
	var result := []
	for value in values:
		if not result.has(value):
			result.append(value)
	return result
