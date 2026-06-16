## Real-node robustness under seeded impairment (ports the lag-comp spike tier D).
##
## Under jitter and loss the server sees a thinned sample of the input stream, so
## divergence appears. The invariant is that each correction absorbs it: the
## residual stays bounded, replay depth never spirals, and the loop reaches a
## stable steady state instead of a permanent desync or a death spiral. Proven on
## the production [PredictionComponent] through [PredictionScenario].
class_name TestPredictionRobustness
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }


func test_bounded_under_jitter_no_spiral() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(3, 6, 0.05)
	s.hold_input(p, RIGHT)
	s.run(160)

	# Replay walks only the in-flight window every correction. A spiral would
	# grow this without bound; it must stay near the RTT depth.
	assert_int(p.max_replay_depth).is_less(30)
	# Residual stays bounded: a single thinned-input drift, not a runaway.
	assert_float(p.peak_divergence()).is_less(400.0)
	assert_int(p.consumed).is_greater(20)


func test_recovers_after_heavy_loss_burst() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4, 8, 0.12)
	s.hold_input(p, RIGHT)
	s.run(140)
	# Drop the impairment and let it settle: the residual must shrink back down,
	# proving corrections converge rather than accumulate.
	s.latency_both(4, 0, 0.0)
	s.run(40)

	assert_int(p.max_replay_depth).is_less(40)
	assert_float(p.tail_divergence(6)).is_less(p.peak_divergence() + 0.001)
	assert_int(p.observer.divergence_log.size()).is_greater(20)
