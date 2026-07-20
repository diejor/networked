## Extrapolated SNAP restore (lag-comp I5, the prediction twin of forecast).
##
## A [constant PredictionComponent.CorrectionMode.SNAP] restore normally lands the
## authoritative state verbatim at its own tick, so a dynamic body snaps back to a
## stale position. [constant PredictionComponent.RestoreMode.EXTRAPOLATED] projects
## each field that declares a [method NetwInterpolate.project_by] velocity forward
## to the present tick through [NetwProject], so the body lands near where it is.
## The projection metadata is read off the state set specs, so a field with no
## replicated velocity restores verbatim.
class_name TestExtrapolatedRestore
extends NetwTestSuite

const SNAP := PredictionComponent.CorrectionMode.SNAP
const EXACT := PredictionComponent.RestoreMode.EXACT
const EXTRAPOLATED := PredictionComponent.RestoreMode.EXTRAPOLATED
const RIGHT := { &"motion": Vector2.RIGHT, &"bombing": false }


func test_snap_restore_defaults_to_exact() -> void:
	var pred: PredictionComponent = auto_free(PredictionComponent.new())
	assert_int(pred.snap_restore).is_equal(EXACT)


# The extrapolated restore carries a dynamic body forward by its replicated
# velocity, so under a steady +x drift it lands ahead of an exact restore that
# snaps back to the stale authoritative tick. Both entities share one scenario and
# the same scripted input and perturbation, so the only difference is the mode.
func test_extrapolated_restore_lands_ahead_of_exact() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var exact := await s.add_predicted_entity([&"position", &"velocity"])
	var extrap := await s.add_predicted_entity([&"position", &"velocity"])
	exact.client_prediction.correction_mode = SNAP
	exact.client_prediction.snap_restore = EXACT
	extrap.client_prediction.correction_mode = SNAP
	extrap.client_prediction.snap_restore = EXTRAPOLATED

	s.latency_both(4)
	s.hold_input(exact, RIGHT)
	s.hold_input(extrap, RIGHT)
	s.run(20)
	# A transverse nudge the client never predicted forces a correction on both.
	s.perturb_server(exact, Vector2(0.0, -40.0))
	s.perturb_server(extrap, Vector2(0.0, -40.0))
	s.run(40)

	assert_int(exact.corrections).is_greater_equal(1)
	assert_int(extrap.corrections).is_greater_equal(1)
	assert_float(extrap.client_body.position.x) \
		.override_failure_message(
			"extrapolated restore x %.2f did not lead exact x %.2f" % [
				extrap.client_body.position.x, exact.client_body.position.x,
			],
		).is_greater(exact.client_body.position.x)


# A field with no replicated velocity has no derivative pair, so EXTRAPOLATED reads
# an empty projection map and restores exactly like EXACT. The plain body replicates
# only position, so the two modes land on the same point.
func test_extrapolated_without_velocity_pair_matches_exact() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var exact := await s.add_predicted_entity()
	var extrap := await s.add_predicted_entity()
	exact.client_prediction.correction_mode = SNAP
	exact.client_prediction.snap_restore = EXACT
	extrap.client_prediction.correction_mode = SNAP
	extrap.client_prediction.snap_restore = EXTRAPOLATED

	s.latency_both(4)
	s.hold_input(exact, RIGHT)
	s.hold_input(extrap, RIGHT)
	s.run(20)
	s.perturb_server(exact, Vector2(0.0, -40.0))
	s.perturb_server(extrap, Vector2(0.0, -40.0))
	s.run(40)

	assert_int(extrap.corrections).is_greater_equal(1)
	assert_vector(extrap.client_body.position) \
		.override_failure_message(
			"no velocity pair, yet EXTRAPOLATED %s diverged from EXACT %s" % [
				extrap.client_body.position, exact.client_body.position,
			],
		).is_equal_approx(exact.client_body.position, Vector2(0.001, 0.001))
