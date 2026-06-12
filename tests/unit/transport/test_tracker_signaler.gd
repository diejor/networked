## Unit tests for [TrackerSignaler] room code generation and info_hash calculation.
extends GdUnitTestSuite

func test_default_signaling_generates_20_char_hex_hash() -> void:
	var signaler: TrackerSignaler = auto_free(
		TrackerSignaler.new(["ws://127.0.0.1:9999"]),
	)
	var err := signaler.open("", 1)
	assert_int(err).is_equal(OK)
	var room := signaler.room_id()
	assert_int(room.length()).is_equal(20)
	assert_str(signaler._info_hash).is_equal(room)


func test_namespaced_short_codes_generates_5_char_code() -> void:
	var signaler: TrackerSignaler = auto_free(
		TrackerSignaler.new(
			["ws://127.0.0.1:9999"],
			"my_game_namespace",
			"ABC",
		),
	)
	var err := signaler.open("", 1)
	assert_int(err).is_equal(OK)
	var room := signaler.room_id()
	# The room ID (room_id()) should be the 5-character room code.
	assert_int(room.length()).is_equal(5)
	for i in range(5):
		assert_bool(room[i] in ["A", "B", "C"]).is_true()

	# The info_hash should be the salted SHA1 hash (20 characters).
	assert_int(signaler._info_hash.length()).is_equal(20)
	var expected_hash := ("my_game_namespace:" + room).sha1_text().substr(0, 20)
	assert_str(signaler._info_hash).is_equal(expected_hash)


func test_client_join_uses_passed_room_code() -> void:
	var signaler: TrackerSignaler = auto_free(
		TrackerSignaler.new(
			["ws://127.0.0.1:9999"],
			"my_game_namespace",
		),
	)
	var err := signaler.open("XYZ12", 2)
	assert_int(err).is_equal(OK)
	var room := signaler.room_id()
	assert_str(room).is_equal("XYZ12")
	var expected_hash := ("my_game_namespace:XYZ12").sha1_text().substr(0, 20)
	assert_str(signaler._info_hash).is_equal(expected_hash)


func test_backend_random_signaling_namespace_generation() -> void:
	var backend: TrackerWebRTCBackend = auto_free(TrackerWebRTCBackend.new())
	var ns1 := backend._random_signaling_namespace()
	var ns2 := backend._random_signaling_namespace()
	assert_int(ns1.length()).is_equal(15)
	assert_str(ns1).is_not_equal(ns2)


func test_webrtc_backend_properties() -> void:
	var backend: TrackerWebRTCBackend = auto_free(TrackerWebRTCBackend.new())
	assert_bool(backend.supports_embedded_server()).is_true()

	# Default empty namespace should give 20-char hex hint.
	backend.signaling_namespace = ""
	var hint1: AddressHint = backend.get_address_hint()
	assert_str(hint1.placeholder).is_equal("20-char hex")

	# Non-empty namespace should give 5-char code hint.
	backend.signaling_namespace = "test_ns"
	var hint2: AddressHint = backend.get_address_hint()
	assert_str(hint2.placeholder).is_equal("5-char code")


func test_webtorrent_directory_propagates_properties() -> void:
	var dir: WebTorrentDirectory = auto_free(WebTorrentDirectory.new())

	var lobby1: LobbyInfo = LobbyInfo.make(
		1,
		"Test Lobby 1",
		2,
		8,
		{ "room_hash": "ABCDE" },
	)

	var target1: JoinTarget = dir.make_join_target(lobby1)
	assert_object(target1).is_not_null()
	assert_object(target1.backend).is_not_null()
	assert_bool(target1.backend is TrackerWebRTCBackend).is_true()

	var webrtc1: TrackerWebRTCBackend = (
			target1.backend as TrackerWebRTCBackend
	)
	assert_str(webrtc1.signaling_namespace).is_empty()

	# If lobby has custom signaling_namespace, it propagates that.
	var lobby2: LobbyInfo = LobbyInfo.make(
		2,
		"Test Lobby 2",
		2,
		8,
		{
			"room_hash": "FGHIJ",
			"signaling_namespace": "card_ns",
		},
	)

	var target2: JoinTarget = dir.make_join_target(lobby2)
	var webrtc2: TrackerWebRTCBackend = (
			target2.backend as TrackerWebRTCBackend
	)
	assert_str(webrtc2.signaling_namespace).is_equal("card_ns")


func test_tracker_client_unreachable_fires_once_and_rearms() -> void:
	var tracker := WebTorrentTrackerClient.new()
	var count := [0]
	tracker.unreachable.connect(func(): count[0] += 1)

	var ws := WebSocketPeer.new()
	ws.set_meta("url", "ws://127.0.0.1:1")
	tracker._sockets.append(ws)
	tracker.poll()
	await get_tree().process_frame
	tracker.poll()

	assert_int(count[0]).is_equal(1)

	tracker.connect_to([])
	var ws2 := WebSocketPeer.new()
	ws2.set_meta("url", "ws://127.0.0.1:2")
	tracker._sockets.append(ws2)
	await get_tree().process_frame
	tracker.poll()

	assert_int(count[0]).is_equal(2)
	tracker.close()
