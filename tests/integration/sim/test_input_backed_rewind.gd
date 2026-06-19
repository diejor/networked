## Input-backed rewind history for remote predicted entities.
##
## Server-consumed prediction applies client input by input tick, so the
## server-side [NetwTimeline] must key that resulting state by the same timeline
## the predicting client uses. Otherwise [method NetwLagCompensation.sample]
## reads a stale server-clock slot even when the input has arrived.
class_name TestInputBackedRewind
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }


func test_sample_matches_predicted_state_for_consumed_input() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.warmup(p, 20)
	s.run(80)

	var ack := p.latest_ack
	assert_int(ack).is_greater(0)
	var state_tick := ack + 1
	var predicted := p.predicted_state_at(state_tick)
	var sampled := p.server_sample_at(state_tick)

	assert_bool(predicted.is_empty()).is_false()
	assert_bool(sampled.is_empty()).is_false()
	assert_vector(sampled.position).is_equal_approx(
		predicted[&"position"],
		Vector2.ONE * 0.001,
	)
