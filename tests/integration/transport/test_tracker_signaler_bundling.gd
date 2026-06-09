## Proves [TrackerSignaler] sends offers and answers as directed messages in the
## answer slot a WebTorrent tracker forwards verbatim, each carrying its full ICE
## candidate bundle, instead of the offers[] matchmaking array a tracker strips.
##
## A [FakeTrackerClient] records every announce the signaler makes, so the tests
## assert the wire shape directly with no tracker traffic.
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
