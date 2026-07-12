## Unit tests for the [NetwQuantize] family (round-trip + bounded error).
class_name TestNetwQuantize
extends NetwTestSuite

func _roundtrip(q: NetwQuantize, value: Variant, type: Variant.Type) -> Variant:
	var w := NetwBitBuffer.Writer.new()
	q.write(w, value)
	var r := NetwBitBuffer.Reader.new(w.to_bytes())
	return q.read(r, type)


func _fixed(
		step: float,
		min_value: float,
		max_value: float,
) -> NetwQuantizeFixed:
	var q := NetwQuantizeFixed.new()
	q.resolution_step = step
	q.min_limit = min_value
	q.max_limit = max_value
	return q


func _angle_error(a: Quaternion, b: Quaternion) -> float:
	var an := a.normalized()
	var bn := b.normalized()
	var dot := absf(an.x * bn.x + an.y * bn.y + an.z * bn.z + an.w * bn.w)
	return 2.0 * acos(clampf(dot, -1.0, 1.0))


func test_bits_scalar_and_vector() -> void:
	var q := NetwQuantizeBits.new()
	q.bit_count = 8
	q.min_limit = -1.0
	q.max_limit = 1.0

	# error bound is span / (2^bits - 1) ~= 0.0078
	assert_float(_roundtrip(q, 0.5, TYPE_FLOAT)).is_equal_approx(0.5, 0.01)
	assert_vector(_roundtrip(q, Vector2(0.25, -0.75), TYPE_VECTOR2)) \
			.is_equal_approx(Vector2(0.25, -0.75), Vector2(0.01, 0.01))
	assert_vector(
		_roundtrip(q, Vector3(0.25, -0.75, 0.5), TYPE_VECTOR3),
	).is_equal_approx(
		Vector3(0.25, -0.75, 0.5),
		Vector3(0.01, 0.01, 0.01),
	)
	assert_int(q.bit_width(TYPE_VECTOR3)).is_equal(24)
	assert_int(q.bit_width(TYPE_VECTOR2)).is_equal(16)
	assert_int(q.bit_width(TYPE_FLOAT)).is_equal(8)

	# A symmetric range round-trips its center exactly, so a value at rest on one
	# axis (pure horizontal motion) does not pick up quant noise on the other.
	assert_float(_roundtrip(q, 0.0, TYPE_FLOAT)).is_equal(0.0)
	assert_vector(_roundtrip(q, Vector2(-1.0, 0.0), TYPE_VECTOR2)).is_equal(
		Vector2(-1.0, 0.0),
	)
	assert_vector(
		_roundtrip(q, Vector3(-1.0, 0.0, -1.0), TYPE_VECTOR3),
	).is_equal(
		Vector3(-1.0, 0.0, -1.0),
	)


func test_fixed_is_exact_on_grid() -> void:
	var q := NetwQuantizeFixed.new()
	q.resolution_step = 0.5
	q.min_limit = -100.0
	q.max_limit = 100.0

	# multiples of step round-trip exactly
	assert_vector(_roundtrip(q, Vector2(10.0, -20.5), TYPE_VECTOR2)) \
			.is_equal(Vector2(10.0, -20.5))
	assert_vector(
		_roundtrip(q, Vector3(10.0, -20.5, 30.0), TYPE_VECTOR3),
	).is_equal(
		Vector3(10.0, -20.5, 30.0),
	)
	# off-grid value snaps within half a step
	assert_float(_roundtrip(q, 3.3, TYPE_FLOAT)).is_equal_approx(3.5, 0.26)


func test_max_error_matches_resolution() -> void:
	# Fixed: half a step per axis, magnitude across a Vector2.
	var fixed := NetwQuantizeFixed.new()
	fixed.resolution_step = 0.5
	assert_float(fixed.max_error(TYPE_FLOAT)).is_equal_approx(0.25, 0.0001)
	assert_float(fixed.max_error(TYPE_VECTOR2)) \
			.is_equal_approx(0.25 * sqrt(2.0), 0.0001)
	assert_float(fixed.max_error(TYPE_VECTOR3)) \
			.is_equal_approx(0.25 * sqrt(3.0), 0.0001)

	# Bits: half the grid spacing (span / 2^bits) per axis.
	var bits := NetwQuantizeBits.new()
	bits.bit_count = 8
	bits.min_limit = -1.0
	bits.max_limit = 1.0
	assert_float(bits.max_error(TYPE_FLOAT)).is_equal_approx(
		2.0 / 256.0 * 0.5,
		0.0001,
	)
	assert_float(bits.max_error(TYPE_VECTOR2)) \
			.is_equal_approx(2.0 / 256.0 * 0.5 * sqrt(2.0), 0.0001)
	assert_float(bits.max_error(TYPE_VECTOR3)) \
			.is_equal_approx(
				2.0 / 256.0 * 0.5 * sqrt(3.0),
				0.0001,
			)

	# The round-trip error of a worst-case value never exceeds the reported bound.
	var off_grid := 0.3123
	var decoded: float = _roundtrip(bits, off_grid, TYPE_FLOAT)
	assert_float(absf(decoded - off_grid)).is_less_equal(
		bits.max_error(TYPE_FLOAT),
	)

	# Angle: half the angular resolution in radians.
	var angle := NetwQuantizeAngle.new()
	angle.bit_count = 8
	assert_float(angle.max_error(TYPE_FLOAT)).is_equal_approx(
		TAU / 256.0 * 0.5,
		0.0001,
	)


func test_angle_wraps() -> void:
	var q := NetwQuantizeAngle.new()
	q.bit_count = 8

	# error bound TAU / 256 ~= 0.0245
	assert_float(_roundtrip(q, PI, TYPE_FLOAT)).is_equal_approx(PI, 0.03)
	# TAU wraps back to ~0
	assert_float(_roundtrip(q, TAU, TYPE_FLOAT)).is_equal_approx(0.0, 0.03)
	assert_int(q.bit_width(TYPE_FLOAT)).is_equal(8)


func test_quaternion_uses_smallest_three_layout() -> void:
	var q := NetwQuantizeQuaternion.new()
	q.bit_count = 12

	var axis := Vector3(0.3, 1.0, 0.2).normalized()
	var value := Quaternion(axis, 1.234)
	var got: Quaternion = _roundtrip(q, value, TYPE_QUATERNION)

	assert_int(q.bit_width(TYPE_QUATERNION)).is_equal(38)
	assert_float(_angle_error(value, got)).is_less_equal(
		q.max_error(TYPE_QUATERNION),
	)


func test_transform_2d_composes_origin_rotation_and_scale() -> void:
	var rotation_quantizer := NetwQuantizeAngle.new()
	rotation_quantizer.bit_count = 12

	var q := NetwQuantizeTransform2D.new()
	q.origin_quantizer = _fixed(0.25, -10.0, 10.0)
	q.rotation_quantizer = rotation_quantizer
	q.scale_quantizer = _fixed(0.125, 0.0, 4.0)

	var value := Transform2D(
		PI * 0.25,
		Vector2(2.0, 0.5),
		0.0,
		Vector2(3.25, -4.5),
	)
	var got: Transform2D = _roundtrip(q, value, TYPE_TRANSFORM2D)

	assert_int(q.bit_width(TYPE_TRANSFORM2D)).is_equal(38)
	assert_vector(got.origin).is_equal(Vector2(3.25, -4.5))
	assert_float(got.get_rotation()).is_equal_approx(value.get_rotation(), 0.002)
	assert_vector(got.get_scale()).is_equal_approx(
		value.get_scale(),
		Vector2(0.001, 0.001),
	)


func test_transform_3d_composes_origin_rotation_and_scale() -> void:
	var rotation := Quaternion(Vector3(0.2, 1.0, 0.4).normalized(), 0.9)
	var value := Transform3D(
		Basis(rotation).scaled_local(Vector3(1.5, 0.75, 2.0)),
		Vector3(1.25, -2.5, 3.75),
	)
	var rotation_quantizer := NetwQuantizeQuaternion.new()
	rotation_quantizer.bit_count = 12

	var q := NetwQuantizeTransform3D.new()
	q.origin_quantizer = _fixed(0.25, -10.0, 10.0)
	q.rotation_quantizer = rotation_quantizer
	q.scale_quantizer = _fixed(0.125, 0.0, 4.0)

	var got: Transform3D = _roundtrip(q, value, TYPE_TRANSFORM3D)

	assert_int(q.bit_width(TYPE_TRANSFORM3D)).is_equal(77)
	assert_vector(got.origin).is_equal(Vector3(1.25, -2.5, 3.75))
	assert_float(
		_angle_error(rotation, got.basis.get_rotation_quaternion()),
	).is_less_equal(rotation_quantizer.max_error(TYPE_QUATERNION))
	assert_vector(got.basis.get_scale()).is_equal_approx(
		value.basis.get_scale(),
		Vector3(0.001, 0.001, 0.001),
	)


func test_layout_equality_matches_script_and_parameters() -> void:
	# Two fresh instances with identical parameters are the same schema, the
	# case a per-instance _init re-declaration produces every spawn.
	var a := _fixed(0.25, -10.0, 10.0)
	var b := _fixed(0.25, -10.0, 10.0)
	assert_bool(a.is_same_layout(b)).is_true()

	# A changed parameter is a different bit layout.
	var c := _fixed(0.5, -10.0, 10.0)
	assert_bool(a.is_same_layout(c)).is_false()

	# A different quantizer class is a different layout even when field names
	# overlap, and null never matches an instance.
	var bits := NetwQuantizeBits.new()
	bits.min_limit = -10.0
	bits.max_limit = 10.0
	assert_bool(a.is_same_layout(bits)).is_false()
	assert_bool(a.is_same_layout(null)).is_false()
