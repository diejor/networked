## Unit tests for [InputSynchronizer] ack-bounded input windows.
class_name TestInputWindow
extends NetwTestSuite

var _root: Node2D
var _sync: InputSynchronizer


func before_test() -> void:
	_root = Node2D.new()
	add_child(_root)
	auto_free(_root)

	_sync = InputSynchronizer.new()
	_sync.name = "InputSync"
	_sync.register_property(
		&"motion",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
		false,
		false,
	)
	_root.add_child(_sync)
	_sync.owner = _root
	_sync.root_path = _sync.get_path_to(_root)
	await get_tree().process_frame


func test_window_is_ack_bounded_and_capped() -> void:
	_sync.input_window_size = 4
	var timeline := NetwTimeline.new()
	_sync.timeline = timeline
	for tick in range(10, 16):
		timeline.record_input(tick, { &"motion": Vector2(tick, -tick) })

	_sync.acknowledged_tick = 12
	_sync.authored_tick = 15

	var bytes := _sync._read_property(
		InputSynchronizer.INPUT_WINDOW,
		NodePath(""),
	) as PackedByteArray
	var window := SampleWindowCodec.decode(bytes, [&"motion"] as Array[StringName])

	assert_int(window.size()).is_equal(3)
	assert_int(window[0].tick).is_equal(13)
	assert_int(window[1].tick).is_equal(14)
	assert_int(window[2].tick).is_equal(15)
	assert_vector(window[2].input.motion).is_equal(Vector2(15, -15))


func test_window_uses_newest_cap_when_ack_lags() -> void:
	_sync.input_window_size = 4
	var timeline := NetwTimeline.new()
	_sync.timeline = timeline
	for tick in range(10, 16):
		timeline.record_input(tick, { &"motion": Vector2(tick, -tick) })

	_sync.acknowledged_tick = -1
	_sync.authored_tick = 15

	var bytes := _sync._read_property(
		InputSynchronizer.INPUT_WINDOW,
		NodePath(""),
	) as PackedByteArray
	var window := SampleWindowCodec.decode(bytes, [&"motion"] as Array[StringName])

	assert_int(window.size()).is_equal(4)
	assert_int(window[0].tick).is_equal(12)
	assert_int(window[3].tick).is_equal(15)


func test_snapshot_payload_excludes_input_window() -> void:
	_root.position = Vector2(3, 4)
	var snap := _sync.snapshot_payload()

	assert_vector(snap.motion).is_equal(Vector2(3, 4))
	assert_bool(snap.has(StampedSynchronizer.TICK)).is_false()
	assert_bool(snap.has(InputSynchronizer.INPUT_WINDOW)).is_false()


func test_receiver_records_window_samples_once() -> void:
	var timeline := NetwTimeline.new()
	_sync.timeline = timeline

	var received: Array[int] = []
	_sync.on_input_received = (
			func(tick: int, _payload: Dictionary) -> void:
				received.append(tick)
	)

	var samples: Array[Dictionary] = [
		{ &"tick": 13, &"input": { &"motion": Vector2(13, -13) } },
		{ &"tick": 14, &"input": { &"motion": Vector2(14, -14) } },
		{ &"tick": 15, &"input": { &"motion": Vector2(15, -15) } },
	]
	var bytes := SampleWindowCodec.encode(samples, [&"motion"] as Array[StringName])

	_sync._write_property(StampedSynchronizer.TICK, NodePath(""), 15)
	_sync._write_property(InputSynchronizer.INPUT_WINDOW, NodePath(""), bytes)
	_sync._write_property(&"motion", NodePath(".:position"), Vector2(15, -15))
	_sync._on_synchronized()

	assert_that(received).contains_exactly([13, 14, 15])
	assert_vector(timeline.input_at(13).motion).is_equal(Vector2(13, -13))
	assert_vector(timeline.input_at(15).motion).is_equal(Vector2(15, -15))


func test_receiver_falls_back_to_direct_payload_without_window() -> void:
	var timeline := NetwTimeline.new()
	_sync.timeline = timeline

	_sync._write_property(StampedSynchronizer.TICK, NodePath(""), 15)
	_sync._write_property(&"motion", NodePath(".:position"), Vector2(15, -15))
	_sync._on_synchronized()

	assert_vector(timeline.input_at(15).motion).is_equal(Vector2(15, -15))


func test_codec_roundtrip() -> void:
	# bool/int/Vector2 are raw-packed; float falls back to var_to_bytes.
	var keys: Array[StringName] = [&"flag", &"count", &"motion", &"spin"]
	var samples: Array[Dictionary] = [
		{
			&"tick": 100,
			&"input": { &"flag": true, &"count": 7, &"motion": Vector2(1, 2), &"spin": 0.5 },
		},
		{
			&"tick": 101,
			&"input": { &"flag": false, &"count": -3, &"motion": Vector2(-4, 8), &"spin": -1.25 },
		},
	]

	var decoded := SampleWindowCodec.decode(SampleWindowCodec.encode(samples, keys), keys)

	assert_int(decoded.size()).is_equal(2)
	assert_int(decoded[0].tick).is_equal(100)
	assert_bool(decoded[0].input.flag).is_true()
	assert_int(decoded[0].input.count).is_equal(7)
	assert_vector(decoded[0].input.motion).is_equal(Vector2(1, 2))
	assert_float(decoded[0].input.spin).is_equal_approx(0.5, 0.0001)
	assert_int(decoded[1].tick).is_equal(101)
	assert_bool(decoded[1].input.flag).is_false()
	assert_int(decoded[1].input.count).is_equal(-3)
	assert_vector(decoded[1].input.motion).is_equal(Vector2(-4, 8))
	assert_float(decoded[1].input.spin).is_equal_approx(-1.25, 0.0001)


func test_codec_empty_window_roundtrips_empty() -> void:
	var keys: Array[StringName] = [&"motion"]
	var empty: Array[Dictionary] = []
	var bytes := SampleWindowCodec.encode(empty, keys)
	assert_int(bytes.size()).is_equal(0)
	assert_int(SampleWindowCodec.decode(bytes, keys).size()).is_equal(0)


func test_window_sole_carrier_when_enabled() -> void:
	# before_test leaves input_window_size = 0 (auto) -> window enabled, so
	# finalize() suppresses the standalone payload props to NEVER.
	var motion_mode := -1
	for path: NodePath in _sync.replication_config.get_properties():
		var sub := path.get_subname_count()
		if sub > 0 and StringName(path.get_subname(sub - 1)) == &"motion":
			motion_mode = _sync.replication_config.property_get_replication_mode(path)

	assert_int(motion_mode).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)
	# Still registered and readable for the codec / snapshot_payload.
	assert_bool(_sync.has_virtual_property(&"motion")).is_true()
	_root.position = Vector2(9, 9)
	assert_vector(_sync.snapshot_payload().motion).is_equal(Vector2(9, 9))


func test_effective_window_size_derivation() -> void:
	# Explicit positive value wins.
	_sync.input_window_size = 7
	assert_int(_sync._effective_window_size()).is_equal(7)

	# Auto (0) with no resolvable clock falls back to 4.
	_sync.input_window_size = 0
	assert_int(_sync._effective_window_size()).is_equal(4)
