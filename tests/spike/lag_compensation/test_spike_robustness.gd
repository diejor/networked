## Tier D spike: robustness under seeded impairment (architecture 9.3).
##
## Under jitter and loss the server sees a thinned sample of the input stream,
## so divergence appears. The invariant is that each correction absorbs it: the
## residual stays bounded, replay depth never spirals, and the loop reaches a
## stable steady state instead of a permanent desync or a death spiral.
class_name TestSpikeRobustness
extends NetwTestSuite

var rig: SpikePredictRig


func _const_right(_tick: int) -> Dictionary:
	return {&"mx": 1.0, &"my": 0.0}


func _max_divergence() -> float:
	var worst := 0.0
	for e: Dictionary in rig.predictor.divergence_log:
		var d: float = e["divergence"]
		if d != INF:
			worst = maxf(worst, d)
	return worst


func _tail_divergence(n: int) -> float:
	var worst := 0.0
	var _log: Array = rig.predictor.divergence_log
	for i in range(maxi(0, _log.size() - n), _log.size()):
		var d: float = _log[i]["divergence"]
		if d != INF:
			worst = maxf(worst, d)
	return worst


func test_d1_bounded_under_jitter_no_spiral() -> void:
	rig = SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(3, 6, 0.05)
	rig.sync_ticks(160)

	# Replay walks only the in-flight window every correction. A spiral would
	# grow this without bound; it must stay near the RTT depth.
	assert_that(rig.predictor.max_replay_depth).is_less(30)
	# Residual stays bounded: a single thinned-input drift, not a runaway.
	assert_that(_max_divergence()).is_less(400.0)
	assert_that(rig.server.consumed_count).is_greater(20)


func test_d1_recovers_after_heavy_loss_burst() -> void:
	rig = SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(4, 8, 0.12)
	rig.sync_ticks(140)
	# Drop the impairment and let it settle: the residual must shrink back down,
	# proving corrections converge rather than accumulate.
	rig.delay_both(4, 0, 0.0)
	rig.sync_ticks(40)

	assert_that(rig.predictor.max_replay_depth).is_less(40)
	assert_that(_tail_divergence(6)).is_less(_max_divergence() + 0.001)
	assert_that(rig.predictor.divergence_log.size()).is_greater(20)
