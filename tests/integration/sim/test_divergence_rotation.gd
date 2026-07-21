## Per-property divergence with rotation values (lag-comp Tier 2 3D support).
##
## The reconciliation divergence check must read [Quaternion] and [Basis]
## rotation, or a 3D body reads [code]INF[/code] on any rotation difference and
## corrects every frame. The threshold is per property because a 3D body mixes
## meters, radians, and m·s⁻¹, which no single epsilon can serve. These are pure
## checks on [method PredictionComponent.value_error] and the per-property
## decision, so they need no loopback.
class_name TestDivergenceRotation
extends NetwTestSuite

func _component() -> PredictionComponent:
	return auto_free(PredictionComponent.new())


func test_quaternion_error_is_finite_angle() -> void:
	var q0 := Quaternion.IDENTITY
	var q1 := Quaternion(Vector3.UP, 0.5)

	assert_float(PredictionComponent.value_error(q0, q1)).is_equal_approx(0.5, 0.001)


func test_basis_error_is_finite_angle() -> void:
	var b0 := Basis.IDENTITY
	var b1 := Basis(Vector3.UP, 0.3)

	assert_float(PredictionComponent.value_error(b0, b1)).is_equal_approx(0.3, 0.001)


func test_per_property_threshold_overrides_default() -> void:
	var pred := { &"rotation": Quaternion.IDENTITY }
	var auth := { &"rotation": Quaternion(Vector3.UP, 0.5) }

	# 0.5 rad over the 0.01 default counts as diverged.
	assert_bool(PredictionComponent.diverged(pred, auth, 0.01, { })).is_true()

	# A wider per-property threshold absorbs the same rotation difference.
	assert_bool(
		PredictionComponent.diverged(pred, auth, 0.01, { &"rotation": 1.0 }),
	).is_false()


func test_missing_key_forces_correction() -> void:
	var pred := { }
	var auth := { &"position": Vector3.ZERO }

	assert_bool(PredictionComponent.diverged(pred, auth, 0.01, { })).is_true()

# --- the per-field threshold is a property mark ---


# A probe declaring its own tolerance in its own units, the way a real body
# does. The mark is the one source of the per-field threshold.
class EpsilonProbe extends Node2D:
	var spin := Quaternion.IDENTITY

	func _init() -> void:
		Netw.configure_property(self, &"spin").state().epsilon(0.5)


# The epsilon() mark flows from the declaration into the derived state set,
# where the engine and the editor lint both read it. A field without the mark
# reports the inherit sentinel rather than a number it never declared.
func test_epsilon_mark_reaches_the_derived_set() -> void:
	var probe: EpsilonProbe = auto_free(EpsilonProbe.new())
	var set := NetwSyncSet.from_script(
		probe.get_script() as Script,
		NetwSyncSet.Record.RECORD_STATE,
	)

	assert_that(set).is_not_null()
	var spin_field: NetwSyncSet.Field = null
	for field: NetwSyncSet.Field in set.fields:
		if field.key == &"spin":
			spin_field = field
	assert_that(spin_field).is_not_null()
	assert_float(spin_field.epsilon_override).is_equal_approx(0.5, 0.0001)
