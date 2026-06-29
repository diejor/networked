## Unit tests for the [NetwQuantize] family (round-trip + bounded error).
class_name TestNetwQuantize
extends NetwTestSuite

func _roundtrip(q: NetwQuantize, value: Variant, type: Variant.Type) -> Variant:
	var w := NetwBitBuffer.Writer.new()
	q.write(w, value)
	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	return q.read(r, type)


func test_bits_scalar_and_vector() -> void:
	var q := NetwQuantizeBits.new()
	q.bits = 8
	q.min_value = -1.0
	q.max_value = 1.0

	# error bound is span / (2^bits - 1) ~= 0.0078
	assert_float(_roundtrip(q, 0.5, TYPE_FLOAT)).is_equal_approx(0.5, 0.01)
	assert_vector(_roundtrip(q, Vector2(0.25, -0.75), TYPE_VECTOR2)) \
			.is_equal_approx(Vector2(0.25, -0.75), Vector2(0.01, 0.01))
	assert_int(q.bit_width(TYPE_VECTOR2)).is_equal(16)
	assert_int(q.bit_width(TYPE_FLOAT)).is_equal(8)

	# A symmetric range round-trips its center exactly, so a value at rest on one
	# axis (pure horizontal motion) does not pick up quant noise on the other.
	assert_float(_roundtrip(q, 0.0, TYPE_FLOAT)).is_equal(0.0)
	assert_vector(_roundtrip(q, Vector2(-1.0, 0.0), TYPE_VECTOR2)).is_equal(
		Vector2(-1.0, 0.0),
	)


func test_fixed_is_exact_on_grid() -> void:
	var q := NetwQuantizeFixed.new()
	q.step = 0.5
	q.min_value = -100.0
	q.max_value = 100.0

	# multiples of step round-trip exactly
	assert_vector(_roundtrip(q, Vector2(10.0, -20.5), TYPE_VECTOR2)) \
			.is_equal(Vector2(10.0, -20.5))
	# off-grid value snaps within half a step
	assert_float(_roundtrip(q, 3.3, TYPE_FLOAT)).is_equal_approx(3.5, 0.26)


func test_max_error_matches_resolution() -> void:
	# Fixed: half a step per axis, magnitude across a Vector2.
	var fixed := NetwQuantizeFixed.new()
	fixed.step = 0.5
	assert_float(fixed.max_error(TYPE_FLOAT)).is_equal_approx(0.25, 0.0001)
	assert_float(fixed.max_error(TYPE_VECTOR2)) \
			.is_equal_approx(0.25 * sqrt(2.0), 0.0001)

	# Bits: half the grid spacing (span / 2^bits) per axis.
	var bits := NetwQuantizeBits.new()
	bits.bits = 8
	bits.min_value = -1.0
	bits.max_value = 1.0
	assert_float(bits.max_error(TYPE_FLOAT)).is_equal_approx(2.0 / 256.0 * 0.5, 0.0001)

	# The round-trip error of a worst-case value never exceeds the reported bound.
	var off_grid := 0.3123
	var decoded: float = _roundtrip(bits, off_grid, TYPE_FLOAT)
	assert_float(absf(decoded - off_grid)).is_less_equal(bits.max_error(TYPE_FLOAT))

	# Angle: half the angular resolution in radians.
	var angle := NetwQuantizeAngle.new()
	angle.bits = 8
	assert_float(angle.max_error(TYPE_FLOAT)).is_equal_approx(TAU / 256.0 * 0.5, 0.0001)


func test_angle_wraps() -> void:
	var q := NetwQuantizeAngle.new()
	q.bits = 8

	# error bound TAU / 256 ~= 0.0245
	assert_float(_roundtrip(q, PI, TYPE_FLOAT)).is_equal_approx(PI, 0.03)
	# TAU wraps back to ~0
	assert_float(_roundtrip(q, TAU, TYPE_FLOAT)).is_equal_approx(0.0, 0.03)
	assert_int(q.bit_width(TYPE_FLOAT)).is_equal(8)
