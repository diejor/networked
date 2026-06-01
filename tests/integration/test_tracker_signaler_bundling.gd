## Proves [TrackerSignaler] tunnels ICE through the offer and answer slots a
## WebTorrent tracker actually routes, instead of the broken trickle paths.
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


func _cand(name: String) -> Dictionary:
	return {
		"type": "candidate", "candidate": name, "sdpMid": "0", "sdpMLineIndex": 0,
	}


func test_client_offer_bundles_candidates_with_stable_id() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	sig.send(1, "", "offer", { "type": "offer", "sdp": "OFFER" })
	sig.send(1, "", "candidate", _cand("c1"))
	sig.send(1, "", "candidate", _cand("c2"))
	sig.poll(0.5)  # past the default 0.4s gather grace

	var offers := sig.fake.sdp_announces("offer")
	assert_array(offers).has_size(1)
	var entry: Dictionary = offers[0]["offers"][0]
	var offer_id := String(entry["offer_id"])
	assert_str(offer_id).is_not_empty()
	assert_int((entry["offer"]["candidates"] as Array).size()).is_equal(2)

	# No candidate ever leaves in its own announce while trickle is disabled.
	for data: Dictionary in sig.fake.announces:
		for slot: Variant in data.get("offers", []):
			assert_str(String((slot as Dictionary)["offer"].get("type"))) \
				.is_equal("offer")

	# Re-announcing to reach a late host keeps the same offer_id.
	sig.poll(2.5)
	var offers2 := sig.fake.sdp_announces("offer")
	assert_int(offers2.size()).is_greater_equal(2)
	assert_str(String(offers2[-1]["offers"][0]["offer_id"])).is_equal(offer_id)


func test_host_answer_bundles_candidates_and_reuses_offer_id() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("", 1)
	var room := sig.room_id()
	var client_peer := "00000000000000000002"

	# Inbound client offer registers the offer_id the answer must reuse.
	sig._parse_packet({
		"info_hash": room, "peer_id": client_peer,
		"offer": { "type": "offer", "sdp": "CLIENT_OFFER" }, "offer_id": "OID123",
	})

	sig.send(0, client_peer, "answer", { "type": "answer", "sdp": "ANSWER" })
	sig.send(0, client_peer, "candidate", _cand("h1"))
	sig.poll(0.5)

	var answers := sig.fake.sdp_announces("answer")
	assert_array(answers).has_size(1)
	var msg: Dictionary = answers[0]
	assert_str(String(msg["offer_id"])).is_equal("OID123")
	assert_str(String(msg["to_peer_id"])).is_equal(client_peer)
	assert_int((msg["answer"]["candidates"] as Array).size()).is_equal(1)

	# Draining again must not resend the answer onto the consumed offer_id.
	sig.poll(0.5)
	assert_array(sig.fake.sdp_announces("answer")).has_size(1)


func test_inbound_bundle_fans_out_to_offer_then_candidates() -> void:
	var sig := RecordingSignaler.new(["wss://example"])
	sig.open("a1b2c3d4e5f60718293a", 2)
	var room := sig.room_id()
	var got: Array = []
	sig.received.connect(
		func(_id: int, _peer: String, kind: String, _payload: Dictionary) -> void:
			got.append(kind)
	)

	sig._parse_packet({
		"info_hash": room, "peer_id": "00000000000000000001",
		"answer": {
			"type": "answer", "sdp": "S",
			"candidates": [_cand("c1"), _cand("c2")],
		},
	})

	assert_array(got).is_equal(["answer", "candidate", "candidate"])
