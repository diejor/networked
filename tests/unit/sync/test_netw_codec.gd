## Unit tests for [NetwCodec] snapshot and window framings.
class_name TestNetwCodec
extends NetwTestSuite


func _bits(b: int, lo: float, hi: float) -> NetwQuantizeBits:
	var q := NetwQuantizeBits.new()
	q.bits = b
	q.min_value = lo
	q.max_value = hi
	return q


func test_raw_payload_roundtrip() -> void:
	# No quantizers: byte-aligned, inline type tags. Mixed types.
	var keys: Array[StringName] = [&"flag", &"count", &"motion", &"spin"]
	var quantizers: Array = []
	var types: Array = []
	var payload := {
		&"flag": true, &"count": 7, &"motion": Vector2(1, 2), &"spin": 0.5,
	}

	var w := NetwBitBuffer.Writer.new()
	NetwCodec.encode_payload(w, payload, keys, quantizers)
	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	var got := NetwCodec.decode_payload(r, keys, quantizers, types)

	assert_bool(got.flag).is_true()
	assert_int(got.count).is_equal(7)
	assert_vector(got.motion).is_equal(Vector2(1, 2))
	assert_float(got.spin).is_equal_approx(0.5, 0.0001)


func test_quantized_window_roundtrip() -> void:
	var keys: Array[StringName] = [&"motion"]
	var quantizers: Array = [_bits(8, -1.0, 1.0)]
	var types: Array = [TYPE_VECTOR2]
	var samples: Array[Dictionary] = [
		{ &"tick": 13, &"input": { &"motion": Vector2(1, 0) } },
		{ &"tick": 14, &"input": { &"motion": Vector2(0.5, -0.5) } },
		{ &"tick": 15, &"input": { &"motion": Vector2(-1, 1) } },
	]

	var bytes := NetwCodec.encode_window(samples, keys, quantizers)
	var got := NetwCodec.decode_window(bytes, keys, quantizers, types)

	assert_int(got.size()).is_equal(3)
	assert_int(got[0].tick).is_equal(13)
	assert_int(got[2].tick).is_equal(15)
	assert_vector(got[1].input.motion).is_equal_approx(Vector2(0.5, -0.5), Vector2(0.01, 0.01))
	assert_vector(got[2].input.motion).is_equal_approx(Vector2(-1, 1), Vector2(0.01, 0.01))


func test_snapshot_mixed_quantized_and_raw() -> void:
	var keys: Array[StringName] = [&"position", &"velocity", &"stunned"]
	var fixed := NetwQuantizeFixed.new()
	fixed.step = 0.5
	fixed.min_value = -1000.0
	fixed.max_value = 1000.0
	var quantizers: Array = [fixed, _bits(8, -90.0, 90.0), null]
	var types: Array = [TYPE_VECTOR2, TYPE_VECTOR2, TYPE_BOOL]
	var payload := {
		&"position": Vector2(10.0, -20.5),
		&"velocity": Vector2(30.0, -45.0),
		&"stunned": true,
	}

	var bytes := NetwCodec.encode_snapshot(42, 39, payload, keys, quantizers)
	var frame := NetwCodec.decode_snapshot(bytes, keys, quantizers, types)

	assert_int(frame.tick).is_equal(42)
	assert_int(frame.ack).is_equal(39)
	assert_vector(frame.payload.position).is_equal(Vector2(10.0, -20.5))
	assert_vector(frame.payload.velocity).is_equal_approx(Vector2(30.0, -45.0), Vector2(1.0, 1.0))
	assert_bool(frame.payload.stunned).is_true()


func test_snapshot_ack_negative_one() -> void:
	var keys: Array[StringName] = [&"position"]
	var quantizers: Array = [null]
	var types: Array = [TYPE_VECTOR2]
	var bytes := NetwCodec.encode_snapshot(7, -1, { &"position": Vector2(3, 4) }, keys, quantizers)
	var frame := NetwCodec.decode_snapshot(bytes, keys, quantizers, types)
	assert_int(frame.ack).is_equal(-1)
	assert_vector(frame.payload.position).is_equal(Vector2(3, 4))


func test_empty_window_and_snapshot() -> void:
	var keys: Array[StringName] = [&"motion"]
	var empty: Array[Dictionary] = []
	assert_int(NetwCodec.encode_window(empty, keys, []).size()).is_equal(0)
	assert_int(NetwCodec.decode_window(PackedByteArray(), keys, [], []).size()).is_equal(0)
	assert_bool(NetwCodec.decode_snapshot(PackedByteArray(), keys, [], []).is_empty()).is_true()
