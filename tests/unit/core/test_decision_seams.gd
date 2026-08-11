## Laws for [method NetwMultiplayer.overrides_seam].
##
## Every prediction decision is answered natively wherever nobody replaced it,
## so what a native answer defers to is this predicate and nothing else. It has
## to separate a seam a session REDECLARED from one it merely inherits, which a
## script's method list does not state directly: the list carries inherited
## entries too, and only the count separates the two.
class_name TestDecisionSeams
extends NetwTestSuite


class RecoverOnly extends NetwMultiplayer:
	func _predict_recover(
			payload: Dictionary,
			policy: int,
			correction: int,
			snap_restore: int,
			projection: Dictionary,
			current: Dictionary,
			pose_errors: Dictionary,
			wiring: NetwPredict.Wiring,
			verdict: NetwPredict.Verdict,
			tick_delta: float,
	) -> Dictionary:
		var plan := super(
			payload,
			policy,
			correction,
			snap_restore,
			projection,
			current,
			pose_errors,
			wiring,
			verdict,
			tick_delta,
		)
		plan[&"skip"] = true
		return plan


class Deeper extends RecoverOnly:
	pass


## Verify a stock session claims no seam, which is what lets the pool answer
## every decision in an ordinary run.
func test_a_stock_session_overrides_nothing() -> void:
	var api := NetwMultiplayer.new()
	assert_bool(api.overrides_seam(&"_predict_recover")).is_false()
	assert_bool(api.overrides_seam(&"_predict_evaluate")).is_false()


## Verify the predicate answers per SEAM. A session that replaced one decision
## has not replaced the others, and reporting otherwise would hand the whole
## prediction plane back to GDScript for one override.
func test_an_override_claims_only_its_own_seam() -> void:
	var api := RecoverOnly.new()
	assert_bool(api.overrides_seam(&"_predict_recover")).is_true()
	assert_bool(api.overrides_seam(&"_predict_evaluate")).is_false()


## Verify a subclass of a subclass still reports the seam its base replaced,
## since the override is inherited even where the leaf declares nothing.
func test_an_inherited_override_still_counts() -> void:
	var api := Deeper.new()
	assert_bool(api.overrides_seam(&"_predict_recover")).is_true()
	assert_bool(api.overrides_seam(&"_predict_evaluate")).is_false()
