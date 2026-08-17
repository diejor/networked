## Unit tests for [SessionCore] driven straight from the peer
## assignment edge, with no [MultiplayerTree] and no host or join verb.
##
## Proves the drop-in contract: a bare [code]multiplayer_peer = peer[/code] moves
## the session machine on its own.
class_name TestSessionAssignmentEdge
extends NetwTestSuite

const AuthProtocol := preload("res://addons/networked/session/auth/auth_protocol.gd")


class _Probe extends Node:
	pass


class _SubProbe extends _Probe:
	pass


class _FailingAuth extends NetwAuthFlow:
	func prepare(_payload: JoinPayload) -> Error:
		return ERR_UNAUTHORIZED


class _RecordingSession extends SessionCore:
	var submissions: Array[JoinPayload] = []


	func submit_join(payload: JoinPayload) -> void:
		submissions.append(payload)
		super.submit_join(payload)


var _apis: Array[NetwMultiplayer] = []


# A bare NetwMultiplayer holds a reference cycle with its session, so it never
# frees on refcount alone. Disposing it after each test breaks that cycle.
func after_test() -> void:
	for api in _apis:
		if is_instance_valid(api) and not api._disposing:
			api.embedding.dispose()
	_apis.clear()
	await super.after_test()


func _bare_api() -> NetwMultiplayer:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	_apis.append(api)
	return api


func _recording_api() -> NetwMultiplayer:
	var api := _bare_api()
	var prior := api._session
	api.connected_to_server.disconnect(prior._on_inner_connected)
	api.connection_failed.disconnect(prior._on_inner_connect_failed)
	api.server_disconnected.disconnect(prior._on_server_dropped)
	prior.dispose()
	api._session = _RecordingSession.new(api)
	return api


func _payload(username: StringName) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	return payload


func _client_peer() -> LocalMultiplayerPeer:
	var server := LocalMultiplayerPeer.new()
	server.create_server()
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)
	server.force_connect_peer(7, peer)
	peer.force_connect_peer(1, server)
	peer.set_meta(&"_test_server", server)
	return peer


func test_service_registry_works_tree_less() -> void:
	var api := _bare_api()
	var svc := _Probe.new()
	var announced: Array[Node] = []
	api.service_registered.connect(func(s: Node) -> void: announced.append(s))

	api.register_service(svc)

	assert_object(api.get_service(_Probe)).is_same(svc)
	assert_int(announced.size()).is_equal(1)

	var retired: Array[Node] = []
	api.service_unregistered.connect(func(s: Node) -> void: retired.append(s))
	api.unregister_service(svc)

	assert_object(api.get_service(_Probe)).is_null()
	assert_int(retired.size()).is_equal(1)
	svc.free()


func test_get_services_is_subclass_aware() -> void:
	var api := _bare_api()
	var sub := _SubProbe.new()
	api.register_service(sub)

	# A subclass registers under its own script yet answers a family query.
	assert_int(api.get_services(_Probe).size()).is_equal(1)
	assert_object(api.get_services(_Probe)[0]).is_same(sub)
	sub.free()


func test_offline_at_construction() -> void:
	var api := _bare_api()
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)
	assert_int(api.role).is_equal(NetwMultiplayer.Role.NONE)
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)
	assert_int(api.role).is_equal(NetwMultiplayer.Role.NONE)


func test_scene_multiplayer_surface_is_mirrored() -> void:
	var api := _bare_api()
	var properties := [
		&"root_path",
		&"auth_callback",
		&"auth_timeout",
		&"allow_object_decoding",
		&"refuse_new_connections",
		&"server_relay",
		&"max_sync_packet_size",
		&"max_delta_packet_size",
	]
	for property: StringName in properties:
		assert_bool(property in api).is_true()

	# peer_* breaks the verbatim SceneMultiplayer name mirror on purpose: the
	# family the verbs address outranks the mirror. clear() stays because it is
	# a MultiplayerAPI virtual, get_authenticating_peers() because it addresses
	# no single peer.
	for method: StringName in [
		&"clear",
		&"peer_disconnect",
		&"get_authenticating_peers",
		&"peer_send_auth",
		&"peer_complete_auth",
		&"send_bytes",
	]:
		assert_bool(api.has_method(method)).is_true()

	for signal_name: StringName in [
		&"peer_authenticating",
		&"peer_authentication_failed",
		&"peer_packet",
	]:
		assert_bool(api.has_signal(signal_name)).is_true()


func test_scene_multiplayer_properties_forward_both_ways() -> void:
	var api := _bare_api()
	api.root_path = ^"/root"
	api.auth_timeout = 7.0
	api.allow_object_decoding = true
	api.refuse_new_connections = true
	api.server_relay = false
	api.max_sync_packet_size = 2048
	api.max_delta_packet_size = 512

	assert_that(api.inner.root_path).is_equal(^"/root")
	assert_float(api.inner.auth_timeout).is_equal(7.0)
	assert_bool(api.inner.allow_object_decoding).is_true()
	assert_bool(api.inner.refuse_new_connections).is_true()
	assert_bool(api.inner.server_relay).is_false()
	assert_int(api.inner.max_sync_packet_size).is_equal(2048)
	assert_int(api.inner.max_delta_packet_size).is_equal(512)

	api.inner.auth_timeout = 9.0
	api.inner.server_relay = true
	assert_float(api.auth_timeout).is_equal(9.0)
	assert_bool(api.server_relay).is_true()


func test_auth_dispatcher_arms_tree_less() -> void:
	var api := _bare_api()

	api._session.configure(NetwSessionConfig.new())

	assert_bool(api.inner.auth_callback.is_valid()).is_true()


func test_auth_config_rearms_dispatcher_tree_less() -> void:
	var api := _bare_api()
	var config := NetwSessionConfig.new()
	config.app_id = &"assignment-edge-auth"

	api._session.configure(config)

	assert_bool(api.inner.auth_callback.is_valid()).is_true()
	api.connected_to_server.emit()
	assert_bool(api.inner.auth_callback.is_valid()).is_true()
	var payload := JoinPayload.new()
	payload.username = &"reconnect"
	assert_int(await api._session.prepare_join(payload)).is_equal(OK)
	assert_bool(api.inner.auth_callback.is_valid()).is_true()


func test_auth_provider_prepares_and_synthesizes_host_tree_less() -> void:
	var api := _bare_api()
	var config := NetwSessionConfig.new()
	api._session.configure(config)
	api.session.set_auth_flow(DummyAuth.new())
	var payload := JoinPayload.new()
	payload.username = &"host"

	var prepare_err := await api._session.prepare_join(payload)
	var peer := LocalMultiplayerPeer.new()
	peer.create_server()
	api.multiplayer_peer = peer

	assert_int(prepare_err).is_equal(OK)
	var bucket := api.peer_get_context(1).get_bucket(NetwIdentityBucket)
	assert_object(bucket.identity).is_not_null()
	assert_str(bucket.identity.username).is_equal(&"host")
	assert_str(bucket.identity.service).is_equal(&"dummy")


func test_application_auth_callback_is_composed_tree_less() -> void:
	var api := _bare_api()
	var received: Array = []
	var user_callback := func(peer_id: int, data: PackedByteArray) -> void:
		received.append([peer_id, data])
	api.auth_callback = user_callback
	var config := NetwSessionConfig.new()
	api._session.configure(config)
	api.session.set_auth_flow(_FailingAuth.new())

	assert_that(api.inner.auth_callback).is_not_equal(user_callback)
	assert_that(api.auth_callback).is_equal(user_callback)
	var payload := JoinPayload.new()
	payload.username = &"application-auth"
	assert_int(await api._session.prepare_join(payload)).is_equal(OK)
	var packet := AuthProtocol.encode_client_hello(
		PackedByteArray([1, 2, 3]),
	)
	api.inner.auth_callback.call(7, packet)
	assert_int(received.size()).is_equal(1)
	assert_int(received[0][0]).is_equal(7)
	assert_array(received[0][1]).is_equal(packet)
	api.connected_to_server.emit()
	assert_bool(api.inner.auth_callback.is_valid()).is_true()
	api._session.deconfigure()
	assert_that(api.auth_callback).is_equal(user_callback)
	assert_bool(api.inner.auth_callback.is_valid()).is_true()


func test_client_assignment_waits_in_connecting() -> void:
	var api := _bare_api()
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)

	api.multiplayer_peer = peer

	# A client is still mid-handshake at assignment, so it holds in CONNECTING
	# until the transport reports the connection.
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.CONNECTING)


func test_cancelled_connect_returns_offline() -> void:
	var api := _bare_api()
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)
	api.multiplayer_peer = peer
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.CONNECTING)

	# Assigning an OfflineMultiplayerPeer is the cancel edge.
	api.multiplayer_peer = OfflineMultiplayerPeer.new()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)


func test_prepared_client_auto_submits_once_on_online() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	var payload := _payload(&"prepared")
	assert_int(await api._session.prepare_join(payload)).is_equal(OK)
	api.multiplayer_peer = _client_peer()

	api.connected_to_server.emit()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)
	assert_int(session.submissions.size()).is_equal(1)
	assert_object(session.submissions[0]).is_same(payload)
	api.connected_to_server.emit()
	assert_int(session.submissions.size()).is_equal(1)


func test_failed_preparation_never_auto_submits() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	var config := NetwSessionConfig.new()
	api._session.configure(config)
	api.session.set_auth_flow(_FailingAuth.new())
	var result := [OK]

	await assert_error(
		func() -> void:
			result[0] = await api._session.prepare_join(
				_payload(&"rejected"),
			)
	).is_push_error("Auth prepare failed: Unauthorized")
	assert_int(result[0]).is_equal(ERR_UNAUTHORIZED)
	api.multiplayer_peer = _client_peer()
	api.connected_to_server.emit()

	assert_int(session.submissions.size()).is_equal(0)


func test_cancelled_connect_discards_prepared_join() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	assert_int(await api._session.prepare_join(_payload(&"cancelled"))) \
			.is_equal(OK)
	api.multiplayer_peer = _client_peer()
	api.multiplayer_peer = OfflineMultiplayerPeer.new()

	api.multiplayer_peer = _client_peer()
	api.connected_to_server.emit()

	assert_int(session.submissions.size()).is_equal(0)


func test_reconnect_auto_submits_newly_prepared_join() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	assert_int(await api._session.prepare_join(_payload(&"stale"))).is_equal(OK)
	api.multiplayer_peer = _client_peer()
	api.multiplayer_peer = OfflineMultiplayerPeer.new()

	var replacement := _payload(&"replacement")
	assert_int(await api._session.prepare_join(replacement)).is_equal(OK)
	api.multiplayer_peer = _client_peer()
	api.connected_to_server.emit()

	assert_int(session.submissions.size()).is_equal(1)
	assert_object(session.submissions[0]).is_same(replacement)


func test_bare_client_assignment_stays_unenriched() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	api.multiplayer_peer = _client_peer()

	api.connected_to_server.emit()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)
	assert_int(session.submissions.size()).is_equal(0)
	assert_int(api.participants.size()).is_equal(0)


func test_listen_server_waits_for_explicit_join_submission() -> void:
	var api := _bare_api()
	var payload := _payload(&"host")
	var local_joins: Array[NetwParticipant] = []
	api.local_participant_joined.connect(
		func(participant: NetwParticipant) -> void:
			local_joins.append(participant)
	)
	assert_int(await api._session.prepare_join(payload)).is_equal(OK)
	var peer := LocalMultiplayerPeer.new()
	peer.create_server()

	api.multiplayer_peer = peer

	assert_int(api.role).is_equal(
		NetwMultiplayer.Role.LISTEN_SERVER,
	)
	assert_object(api.peer_get_participant(1)).is_null()
	api._session.submit_join(payload)

	assert_object(api.peer_get_participant(1)).is_not_null()
	assert_object(api.local_participant).is_same(api.peer_get_participant(1))
	assert_int(local_joins.size()).is_equal(1)


func test_session_pause_and_unpause_work_tree_less() -> void:
	var api := _bare_api()
	var peer := LocalMultiplayerPeer.new()
	peer.create_server()
	api.multiplayer_peer = peer
	var reasons: Array[String] = []
	api.tree_paused.connect(func(reason: String) -> void: reasons.append(reason))
	var resumed := [0]
	api.tree_unpaused.connect(func() -> void: resumed[0] += 1)

	api.session.pause("waiting")

	assert_bool(get_tree().paused).is_true()
	assert_array(reasons).is_equal(["waiting"])

	api.session.unpause()

	assert_bool(get_tree().paused).is_false()
	assert_int(resumed[0]).is_equal(1)


func test_session_kick_disconnects_peer_tree_less() -> void:
	var api := _bare_api()
	var server := LocalMultiplayerPeer.new()
	server.create_server()
	var client := LocalMultiplayerPeer.new()
	client.create_client(7)
	server.force_connect_peer(7, client)
	client.force_connect_peer(1, server)
	api.multiplayer_peer = server

	api.peer_kick(7, "bye")

	assert_bool(server.is_linked_to(7)).is_false()


func test_session_leave_returns_offline_tree_less() -> void:
	var api := _bare_api()
	api.multiplayer_peer = _client_peer()
	api.connected_to_server.emit()
	var timer := get_tree().create_timer(0.01)
	timer.timeout.connect(api.server_disconnected.emit)

	await api.session.leave()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)


func test_auth_provider_path_auto_submits() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	var config := NetwSessionConfig.new()
	api._session.configure(config)
	api.session.set_auth_flow(DummyAuth.new())
	assert_int(await api._session.prepare_join(_payload(&"provider"))) \
			.is_equal(OK)
	api.multiplayer_peer = _client_peer()

	api.connected_to_server.emit()

	assert_int(session.submissions.size()).is_equal(1)


func test_application_auth_path_auto_submits() -> void:
	var api := _recording_api()
	var session := api._session as _RecordingSession
	api.auth_callback = func(_peer_id: int, _data: PackedByteArray) -> void:
		pass
	var config := NetwSessionConfig.new()
	api._session.configure(config)
	api.session.set_auth_flow(_FailingAuth.new())
	assert_int(await api._session.prepare_join(_payload(&"application"))) \
			.is_equal(OK)
	api.multiplayer_peer = _client_peer()

	api.connected_to_server.emit()

	assert_int(session.submissions.size()).is_equal(1)


func test_server_crash_ends_session_tree_less() -> void:
	var api := _bare_api()
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)
	api.multiplayer_peer = peer
	api.connected_to_server.emit()
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)

	var ended := [0]
	api.session_ended.connect(func() -> void: ended[0] += 1)

	# A server that vanishes ends the session straight from the api edge, with no
	# tree in the loop.
	api.server_disconnected.emit()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.OFFLINE)
	assert_int(ended[0]).is_equal(1)

	# A second crash while already offline never re-tears.
	api.server_disconnected.emit()
	assert_int(ended[0]).is_equal(1)


func test_connected_peer_is_a_row_before_it_joins() -> void:
	var api := _bare_api()

	# A connected peer is a roster row immediately, but not a participant until a
	# join enriches it.
	api.peer_connected.emit(7)

	assert_int(api.connected_participants.size()).is_equal(1)
	assert_int(api.connected_participants[0].peer_id).is_equal(7)
	assert_object(api.peer_get_participant(7)).is_null()
	assert_int(api.participants.size()).is_equal(0)

	# Enriching the row with an accepted join promotes it into the joined roster.
	var rj := ResolvedJoin.new()
	rj.peer_id = 7
	rj.username = &"ada"
	api._roster.remember_accepted_join(rj)

	assert_object(api.peer_get_participant(7)).is_not_null()
	assert_int(api.participants.size()).is_equal(1)
	assert_str(api.peer_get_participant(7).username).is_equal(&"ada")
	# The row identity is stable across enrichment.
	assert_int(api.connected_participants.size()).is_equal(1)


func test_dispose_suppresses_crash_teardown() -> void:
	var api := _bare_api()
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(7)
	api.multiplayer_peer = peer
	api.connected_to_server.emit()
	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)

	# A disposing api closes its own peer during teardown, so a crash signal that
	# rides that close must not be mistaken for the server vanishing.
	api.embedding.dispose()
	api.server_disconnected.emit()

	assert_int(api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)


class _JoinProbe extends Node:
	func handle(_rj: ResolvedJoin, tag: StringName, count: int) -> void:
		pass


func test_join_args_round_trip_through_the_session() -> void:
	var probe := _JoinProbe.new()
	var api := _bare_api()
	api.session.set_join_handler(probe.handle)

	var payload := JoinPayload.new()
	payload.username = &"ada"
	payload.arg_values = [&"north", 3]
	NetwJoinCodec.encode(payload, probe.handle)
	assert_bool(payload.arg_bytes.is_empty()).is_false()

	# A fresh payload off the wire decodes back to the same typed args.
	var wire := JoinPayload.new()
	wire.deserialize(payload.serialize())
	assert_bool(NetwJoinCodec.decode(wire, probe.handle)).is_true()
	assert_that(wire.arg_values).is_equal([&"north", 3])
	probe.free()


func test_join_args_schema_mismatch_rejects() -> void:
	var probe := _JoinProbe.new()
	var api := _bare_api()
	api.session.set_join_handler(probe.handle)

	var payload := JoinPayload.new()
	payload.username = &"ada"
	payload.arg_values = [&"north", 3]
	NetwJoinCodec.encode(payload, probe.handle)

	# A client and server that disagree on the handler signature reject.
	var wire := JoinPayload.new()
	wire.deserialize(payload.serialize())
	wire.schema_hash += 1
	assert_bool(NetwJoinCodec.decode(wire, probe.handle)).is_false()
	probe.free()
