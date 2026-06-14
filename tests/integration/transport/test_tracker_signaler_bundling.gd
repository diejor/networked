## Proves [TrackerSignaler] sends offers and answers as directed messages in the
## answer slot a WebTorrent tracker forwards verbatim, each carrying its full ICE
## candidate bundle, instead of the offers[] matchmaking array a tracker strips.
##
## A [FakeTrackerClient] records every announce the signaler makes, so the tests
## assert the wire shape directly with no tracker traffic.
@tool
class_name TestTrackerSignalerBundling
extends NetwTestSuite

# TrackerSignaler that announces into a FakeTrackerClient the test can inspect.
class RecordingSignaler extends TrackerSignaler:
	var fake: FakeTrackerClient


	func _make_tracker() -> WebTorrentTrackerClient:
		fake = FakeTrackerClient.new()
		return fake


class DelayedFakeTrackerClient extends FakeTrackerClient:
	var open := false


	func connect_to(_urls: Array[String]) -> Error:
		connected.emit()
		return OK


	func has_open() -> bool:
		return open


	func open_socket() -> void:
		open = true
		socket_opened.emit(WebSocketPeer.new())


class DelayedRecordingSignaler extends TrackerSignaler:
	var fake: DelayedFakeTrackerClient


	func _make_tracker() -> WebTorrentTrackerClient:
		fake = DelayedFakeTrackerClient.new()
		return fake


func _cand(_name: String) -> Dictionary:
	return {
		"type": "candidate",
		"candidate": _name,
		"sdpMid": "0",
		"sdpMLineIndex": 0,
	}


func test_client_offer_is_directed_to_host_with_bundled_candidates() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	# The session hands the signaler one bundle: SDP plus every candidate.
	sig.send(
		1,
		"",
		"offer",
		{
			"type": "offer",
			"sdp": "OFFER",
			"candidates": [_cand("c1"), _cand("c2")],
		},
	)

	var offers := sig.fake.sdp_announces("offer")
	assert_array(offers).has_size(1)
	var msg: Dictionary = offers[0]
	# Directed at the host's derived address, never the offers[] matchmaking array.
	assert_str(String(msg["to_peer_id"])).is_equal("a1b2c3d4e5" + "0000000001")
	assert_bool(msg.has("offers")).is_false()
	assert_int((msg["answer"]["candidates"] as Array).size()).is_equal(2)

	sig.close()


func test_offer_sent_before_tracker_open_flushes_on_socket_open() -> void:
	var sig := DelayedRecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	sig.send(
		1,
		"",
		"offer",
		{
			"type": "offer",
			"sdp": "OFFER",
			"candidates": [_cand("c1")],
		},
	)

	assert_array(sig.fake.sdp_announces("offer")).is_empty()
	sig.fake.open_socket()

	var offers := sig.fake.sdp_announces("offer")
	assert_array(offers).has_size(1)
	var msg: Dictionary = offers[0]
	assert_str(String(msg["to_peer_id"])).is_equal("a1b2c3d4e5" + "0000000001")
	assert_int((msg["answer"]["candidates"] as Array).size()).is_equal(1)

	sig.close()


func test_host_answer_is_directed_to_client_with_bundled_candidates() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("", 1)
	var room := sig.room_id()
	var client_peer := "00000000000000000002"

	# Inbound client offer in the directed answer slot, so the host learns it.
	var got_peer := [""]
	sig.received.connect(
		func(_id: int, peer: String, _kind: String, _payload: Dictionary) -> void:
			got_peer[0] = peer
	)
	sig._parse_packet(
		{
			"info_hash": room,
			"peer_id": client_peer,
			"answer": { "type": "offer", "sdp": "CLIENT_OFFER" },
		},
	)
	assert_str(got_peer[0]).is_equal(client_peer)

	sig.send(
		0,
		client_peer,
		"answer",
		{ "type": "answer", "sdp": "ANSWER", "candidates": [_cand("h1")] },
	)

	var answers := sig.fake.sdp_announces("answer")
	assert_array(answers).has_size(1)
	var msg: Dictionary = answers[0]
	assert_str(String(msg["to_peer_id"])).is_equal(client_peer)
	assert_int((msg["answer"]["candidates"] as Array).size()).is_equal(1)

	sig.close()


func test_inbound_offer_reports_once_and_dedupes_resends() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	var room := sig.room_id()
	var got: Array = []
	sig.received.connect(
		func(_id: int, _peer: String, kind: String, _payload: Dictionary) -> void:
			got.append(kind)
	)

	var answer_msg := {
		"info_hash": room,
		"peer_id": "00000000000000000001",
		"answer": { "type": "answer", "sdp": "S", "candidates": [_cand("c1")] },
	}
	sig._parse_packet(answer_msg)
	sig._parse_packet(answer_msg) # a reliability re-send carrying the same SDP

	# Reported once; the candidates ride inside the payload, not as own signals.
	assert_array(got).is_equal(["answer"])

	sig.close()


func test_same_sdp_with_new_candidates_reports_as_topup() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	var room := sig.room_id()
	var counts: Array = []
	sig.received.connect(
		func(
				_id: int,
				_peer: String,
				_kind: String,
				payload: Dictionary,
		) -> void:
			counts.append((payload.get("candidates", []) as Array).size())
	)

	var first := {
		"info_hash": room,
		"peer_id": "00000000000000000001",
		"answer": { "type": "answer", "sdp": "S", "candidates": [_cand("c1")] },
	}
	var topup := {
		"info_hash": room,
		"peer_id": "00000000000000000001",
		"answer": {
			"type": "answer",
			"sdp": "S",
			"candidates": [_cand("c1"), _cand("c2")],
		},
	}
	sig._parse_packet(first)
	sig._parse_packet(topup)
	sig._parse_packet(topup)

	assert_array(counts).is_equal([1, 2])

	sig.close()


func test_connect_result_helpers_and_mapping() -> void:
	var r_ok := ConnectResult.ok()
	assert_bool(r_ok.is_ok()).is_true()
	assert_int(r_ok.status).is_equal(ConnectResult.Status.OK)

	var r_timeout := ConnectResult.timed_out("failed timeout")
	assert_bool(r_timeout.is_ok()).is_false()
	assert_int(r_timeout.status).is_equal(ConnectResult.Status.TIMED_OUT)
	assert_str(r_timeout.message).is_equal("failed timeout")

	var r_unreachable := ConnectResult.unreachable(
		&"TURN_UNREACHABLE",
		"unreachable msg",
	)
	assert_int(r_unreachable.status).is_equal(
		ConnectResult.Status.UNREACHABLE,
	)
	assert_str(r_unreachable.detail).is_equal("TURN_UNREACHABLE")
	assert_str(r_unreachable.message).is_equal("unreachable msg")

	var r_refused := ConnectResult.refused("refused msg")
	assert_int(r_refused.status).is_equal(ConnectResult.Status.REFUSED)
	assert_str(r_refused.message).is_equal("refused msg")

	var r_aborted := ConnectResult.aborted("aborted msg")
	assert_int(r_aborted.status).is_equal(ConnectResult.Status.ABORTED)
	assert_str(r_aborted.message).is_equal("aborted msg")

	var r_error := ConnectResult.error("error msg")
	assert_int(r_error.status).is_equal(ConnectResult.Status.ERROR)
	assert_str(r_error.message).is_equal("error msg")

	assert_bool(str(r_ok).contains("ok")).is_true()
	assert_bool(str(r_timeout).contains("timed_out")).is_true()
	assert_bool(str(r_unreachable).contains("unreachable")).is_true()


func test_signaling_unavailable_on_lost() -> void:
	var backend := PairedWebRTCBackend.new()
	var tree := MultiplayerTree.new()
	add_child(tree)

	var failed_results: Array = []
	backend.connect_failed.connect(
		func(result: ConnectResult):
			failed_results.append(result)
	)

	var peer = backend.create_join_peer(tree, "some_room", "Player")
	assert_that(peer).is_not_null()

	backend._signaler.lost.emit()

	assert_int(failed_results.size()).is_equal(1)
	var res: ConnectResult = failed_results[0]
	assert_int(res.status).is_equal(ConnectResult.Status.UNREACHABLE)
	assert_str(res.detail).is_equal("SIGNALING_UNAVAILABLE")

	backend.peer_reset_state()
	tree.queue_free()


func test_signaling_unavailable_on_timeout() -> void:
	var backend := DelayReadyWebRTCBackend.new()
	var tree := MultiplayerTree.new()
	add_child(tree)

	var failed_results: Array = []
	backend.connect_failed.connect(
		func(result: ConnectResult):
			failed_results.append(result)
	)

	var peer = backend.create_join_peer(tree, "some_room", "Player")
	assert_that(peer).is_not_null()

	backend.poll(0.001)
	assert_int(failed_results.size()).is_equal(0)

	OS.delay_msec(200)

	backend.poll(0.001)

	assert_int(failed_results.size()).is_equal(1)
	var res: ConnectResult = failed_results[0]
	assert_int(res.status).is_equal(
		ConnectResult.Status.UNREACHABLE,
	)
	assert_str(res.detail).is_equal("SIGNALING_UNAVAILABLE")

	backend.peer_reset_state()
	tree.queue_free()


func test_filter_ice_servers() -> void:
	var servers: Array[Dictionary] = [
		{ "urls": ["stun:stun.l.google.com:19302"] },
		{
			"urls": ["turn:openrelay.metered.ca:80"],
			"username": "user",
			"credential": "cred",
		},
		{
			"urls": ["turns:openrelay.metered.ca:443?transport=tcp"],
			"username": "user",
			"credential": "cred",
		},
		{
			"urls": ["turns:openrelay.metered.ca:443"],
			"username": "user",
			"credential": "cred",
		},
		{
			"urls": ["turns:other.com:443"],
		},
	]

	var filtered := WebRTCBackend._filter_ice_servers(servers)
	if OS.has_feature("web"):
		assert_int(filtered.size()).is_equal(5)
	else:
		assert_int(filtered.size()).is_equal(2)
		assert_str(filtered[0]["urls"][0]).is_equal(
			"stun:stun.l.google.com:19302",
		)
		assert_str(filtered[1]["urls"][0]).is_equal(
			"turn:openrelay.metered.ca:80",
		)


func test_filter_unsupported_turn_toggle() -> void:
	var backend := DelayReadyWebRTCBackend.new()
	backend.filter_unsupported_turn = false
	var tree := MultiplayerTree.new()
	add_child(tree)

	backend._build_session_and_signaler()
	# Because filter is disabled, it should get the unfiltered servers
	assert_int(backend._session.ice_servers.size()).is_equal(4)

	backend.peer_reset_state()
	tree.queue_free()


class DelayReadySignaler extends WebRTCSignaler:
	func open(_room_id: String, _local_multiplayer_id: int) -> Error:
		return OK


	func poll(_dt: float) -> void:
		pass


	func close() -> void:
		pass


	func send(
			_to_multiplayer_id: int,
			_to_signaler_id: String,
			_kind: String,
			_payload: Dictionary,
	) -> void:
		pass


	func local_signaler_id() -> String:
		return "delay"


	func room_id() -> String:
		return "delay"


class DelayReadyWebRTCBackend extends WebRTCBackend:
	func make_signaler() -> WebRTCSignaler:
		return DelayReadySignaler.new()


	func connect_timeout_hint() -> float:
		return 0.2
