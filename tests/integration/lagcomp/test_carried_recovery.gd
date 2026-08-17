## Law suite for a declared forward model under
## [constant NetwPredict.Schedule.FRAME]: a recovery whose write the field's own
## [method NetwScriptModel.PropertyConfig.carry_step] rule advanced to the
## present rather than writing the acknowledged value back, and the refusal a
## rule earns by disagreeing with the past it is replayed against.
class_name TestCarriedRecovery
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const OFFSET := Vector2(60, -40)
const SETTLE_FRAMES := 30
const RECOVER_FRAMES := 70


func test_a_faithful_rule_carries_the_write_it_answers_with() -> void:
	var s := await _carrying_scenario()
	var p := await _carrying_entity(s)

	var row := await _recovered_row(s, p)

	assert_int(row.carried).override_failure_message(
		"a rule reproducing the transitions it is replayed against advances "
		+ "the write, it is not declined",
	).is_greater(0)
	assert_int(row.infidelity).is_equal(0)


func test_a_rule_that_disagrees_with_the_recorded_past_is_refused() -> void:
	var s := await _carrying_scenario()
	var p := await _carrying_entity(s)
	p.client_root.carry_gain = 2.0

	var row := await _recovered_row(s, p)

	assert_int(row.infidelity).override_failure_message(
		"a rule overstating every transition must be caught by the fidelity "
		+ "gate rather than written to the body",
	).is_greater(0)
	assert_int(row.carried).is_equal(0)
	assert_int(row.declined).is_greater(0)


func _carrying_scenario() -> PredictionScenario:
	var s := PredictionScenario.new()
	await s.setup(self)
	s.body_type = CarriedSimBody
	return s


func _carrying_entity(s: PredictionScenario) -> PredictedEntity:
	return await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
		0.01,
		PredictionComponent.Schedule.FRAME,
	)


# Drives [param p] through one perturbation and answers its position ledger,
# which is where every carry verdict is charged.
func _recovered_row(
		s: PredictionScenario,
		p: PredictedEntity,
) -> NetwPredictionHandle.FieldRecovery:
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run_frames(SETTLE_FRAMES)
	s.perturb_server(p, OFFSET)
	s.run_frames(RECOVER_FRAMES)
	return p.client_prediction.field_recovery[&"position"]
