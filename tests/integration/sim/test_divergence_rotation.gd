## Per-property divergence with rotation values (lag-comp Tier 2 3D support).
##
## The reconciliation divergence check must read [Quaternion] and [Basis]
## rotation, or a 3D body reads [code]INF[/code] on any rotation difference and
## corrects every frame. The threshold is per property because a 3D body mixes
## meters, radians, and m·s⁻¹, which no single epsilon can serve. These are pure
## checks on [method PredictionComponent._value_error] and the per-property
## decision, so they need no loopback.
class_name TestDivergenceRotation
extends NetwTestSuite

func _component() -> PredictionComponent:
	return auto_free(PredictionComponent.new())


func test_quaternion_error_is_finite_angle() -> void:
	var pc := _component()
	var q0 := Quaternion.IDENTITY
	var q1 := Quaternion(Vector3.UP, 0.5)

	assert_float(pc.call("_value_error", q0, q1)).is_equal_approx(0.5, 0.001)


func test_basis_error_is_finite_angle() -> void:
	var pc := _component()
	var b0 := Basis.IDENTITY
	var b1 := Basis(Vector3.UP, 0.3)

	assert_float(pc.call("_value_error", b0, b1)).is_equal_approx(0.3, 0.001)


func test_per_property_threshold_overrides_default() -> void:
	var pc := _component()
	pc.divergence_epsilon = 0.01
	var pred := { &"rotation": Quaternion.IDENTITY }
	var auth := { &"rotation": Quaternion(Vector3.UP, 0.5) }

	# 0.5 rad over the 0.01 default counts as diverged.
	assert_bool(pc.call("_diverged", pred, auth)).is_true()

	# A wider per-property threshold absorbs the same rotation difference.
	pc.divergence_epsilon_overrides = { &"rotation": 1.0 }
	assert_bool(pc.call("_diverged", pred, auth)).is_false()


func test_missing_key_forces_correction() -> void:
	var pc := _component()
	var pred := { }
	var auth := { &"position": Vector3.ZERO }

	assert_bool(pc.call("_diverged", pred, auth)).is_true()

# --- per-property deadzone inspector rows ---


func test_deadzone_row_roundtrips_into_override() -> void:
	var pc := _component()
	pc.divergence_epsilon = 0.01

	pc.call("_set", &"deadzone/rotation", 0.5)

	assert_float(pc.divergence_epsilon_overrides[&"rotation"]).is_equal_approx(0.5, 0.0001)
	assert_float(pc.call("_get", &"deadzone/rotation")).is_equal_approx(0.5, 0.0001)


func test_deadzone_row_defaults_to_global_epsilon() -> void:
	var pc := _component()
	pc.divergence_epsilon = 0.02

	# No override stored: the row reads the global default.
	assert_float(pc.call("_get", &"deadzone/position")).is_equal_approx(0.02, 0.0001)


func test_setting_global_default_clears_the_override() -> void:
	var pc := _component()
	pc.divergence_epsilon = 0.01
	pc.call("_set", &"deadzone/position", 0.5)
	assert_bool(pc.divergence_epsilon_overrides.has(&"position")).is_true()

	# Landing back on the global default (what the revert arrow does) drops it.
	pc.call("_set", &"deadzone/position", 0.01)
	assert_bool(pc.divergence_epsilon_overrides.has(&"position")).is_false()


func test_deadzone_revert_targets_global_epsilon() -> void:
	var pc := _component()
	pc.divergence_epsilon = 0.03
	assert_bool(pc.call("_property_can_revert", &"deadzone/x")).is_false()

	pc.call("_set", &"deadzone/x", 1.0)
	assert_bool(pc.call("_property_can_revert", &"deadzone/x")).is_true()
	assert_float(pc.call("_property_get_revert", &"deadzone/x")).is_equal_approx(0.03, 0.0001)
