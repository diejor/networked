## The shared extrapolation helper: value plus derivative times age.
##
## [NetwProject] is the one place forecast display and the extrapolated restore
## agree on how a value moves forward, so its per-type math is pinned here away
## from either consumer. A straight extrapolation is exact for the linear types
## and a rotation for a quaternion.
class_name TestNetwProject
extends NetwTestSuite


func test_project_advances_linear_types_by_velocity_times_age() -> void:
	assert_float(NetwProject.project(2.0, 3.0, 0.5)).is_equal_approx(3.5, 0.0001)
	assert_vector(NetwProject.project(Vector2(1.0, 1.0), Vector2(4.0, 0.0), 0.25)) \
		.is_equal_approx(Vector2(2.0, 1.0), Vector2(0.0001, 0.0001))
	assert_vector(NetwProject.project(Vector3.ZERO, Vector3(0.0, 10.0, 0.0), 0.1)) \
		.is_equal_approx(Vector3(0.0, 1.0, 0.0), Vector3.ONE * 0.0001)


func test_zero_velocity_holds_the_value() -> void:
	assert_vector(NetwProject.project(Vector2(7.0, 3.0), Vector2.ZERO, 2.0)) \
		.is_equal_approx(Vector2(7.0, 3.0), Vector2(0.0001, 0.0001))


func test_project_rotates_a_quaternion_by_its_angular_velocity() -> void:
	var start := Quaternion.IDENTITY
	# A quarter turn per second about Z, projected a quarter second, is a
	# sixteenth turn.
	var omega := Vector3(0.0, 0.0, PI * 0.5)
	var result: Quaternion = NetwProject.project(start, omega, 0.25)
	var expected := Quaternion(Vector3(0.0, 0.0, 1.0), PI * 0.125)
	assert_float(result.angle_to(expected)).is_less(0.0001)


func test_supports_names_only_the_projectable_types() -> void:
	assert_bool(NetwProject.supports(TYPE_FLOAT)).is_true()
	assert_bool(NetwProject.supports(TYPE_VECTOR2)).is_true()
	assert_bool(NetwProject.supports(TYPE_VECTOR3)).is_true()
	assert_bool(NetwProject.supports(TYPE_QUATERNION)).is_true()
	assert_bool(NetwProject.supports(TYPE_COLOR)).is_false()
	assert_bool(NetwProject.supports(TYPE_INT)).is_false()
	assert_bool(NetwProject.supports(TYPE_STRING)).is_false()
