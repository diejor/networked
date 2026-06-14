## Tier C spike: ack-based reconciliation (architecture P3/P4, the headline).
##
## Falsifies the model the prior docs did not assume and netfox does not test:
## the owning client predicts immediately, the server stamps an ack, and the
## client reconciles on that ack with no clock lead and no determinism contract.
## If C.1 goes red the design reverts to tick-equality rollback.
class_name TestSpikeReconciliation
extends NetwTestSuite

const FIRE_TICK := 25

var rig: SpikePredictRig


func _const_right(_tick: int) -> Dictionary:
	return {&"mx": 1.0, &"my": 0.0}


func _fire_once(tick: int) -> Dictionary:
	return {&"mx": 1.0, &"my": 0.0, &"fire": tick == FIRE_TICK}


func test_c1_clean_predicts_authority_with_no_corrections() -> void:
	# Exact, lossless delivery: the client's prediction must match the server's
	# authoritative computation tick for tick, so no correction ever fires.
	rig = SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(4)
	rig.sync_ticks(80)

	assert_that(rig.server.consumed_count).is_greater(10)
	assert_that(rig.predictor.divergence_log.size()).is_greater(5)
	assert_that(rig.predictor.corrections).is_equal(0)
	assert_that(rig.client_body.position.x).is_greater(0.0)
	assert_that(rig.server_body.position.x).is_greater(0.0)


func test_c1_perturbation_reconverges() -> void:
	# A server-only nudge the client never predicted forces a divergence. The
	# client must snap to authority, replay its pending inputs, and reconverge.
	rig = SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(4)
	rig.sync_ticks(30)
	rig.server.pending_perturbation = Vector2(60.0, -40.0)
	rig.sync_ticks(70)

	assert_that(rig.predictor.corrections).is_greater_equal(1)
	# Steady state after the correction: divergence back under epsilon.
	var tail := _divergence_tail(rig.predictor, 5)
	assert_that(tail).is_less(rig.predictor.divergence_epsilon)


func test_c2_replay_depth_is_bounded() -> void:
	rig = SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(4)
	rig.sync_ticks(30)
	rig.server.pending_perturbation = Vector2(80.0, 0.0)
	rig.sync_ticks(60)

	# Replay walks only the in-flight window (about RTT in ticks), never spirals.
	assert_that(rig.predictor.max_replay_depth).is_greater(0)
	assert_that(rig.predictor.max_replay_depth).is_less(30)


func test_c3_fire_fires_once_despite_replays() -> void:
	rig = SpikePredictRig.new()
	await rig.setup(self, _fire_once)
	rig.delay_both(4)
	rig.sync_ticks(FIRE_TICK - 2)
	# Perturb right after the fire edge so a correction replays across it.
	rig.sync_ticks(4)
	rig.server.pending_perturbation = Vector2(50.0, 50.0)
	rig.sync_ticks(50)

	# The predictor fired on the fresh pass only; replays (is_fresh=false) never
	# re-fire. The server consumed the fire input exactly once.
	assert_that(rig.predictor.fire_count).is_equal(1)
	assert_that(rig.server.fire_count).is_equal(1)
	assert_that(rig.predictor.corrections).is_greater_equal(1)


func test_c4_stall_policy_skips_lost_input() -> void:
	var sim := _consume_with_hole(SpikePrediction.MissingInput.STALL)
	# Ticks 0 and 2 move; the lost tick 1 contributes nothing.
	var step := SpikeSim.SPEED * sim.dt
	assert_that(sim.body.position.x).is_equal_approx(2.0 * step, 0.001)
	assert_that(sim.missing_count).is_equal(1)


func test_c4_repeat_last_policy_carries_input() -> void:
	var sim := _consume_with_hole(SpikePrediction.MissingInput.REPEAT_LAST)
	# The lost tick repeats the prior input, so all three ticks move.
	var step := SpikeSim.SPEED * sim.dt
	assert_that(sim.body.position.x).is_equal_approx(3.0 * step, 0.001)
	assert_that(sim.missing_count).is_equal(1)


func _consume_with_hole(policy: SpikePrediction.MissingInput) -> SpikePrediction:
	var body := Node2D.new()
	auto_free(body)
	var sim := SpikePrediction.new()
	sim.body = body
	sim.dt = 1.0 / 30.0
	sim.missing_policy = policy
	sim.init_consume_standalone()
	sim.feed_input(0, {&"mx": 1.0, &"tick": 0})
	sim.feed_input(2, {&"mx": 1.0, &"tick": 2})
	sim.consume_step(0)
	sim.consume_step(1)
	sim.consume_step(2)
	return sim


func _divergence_tail(p: SpikePrediction, n: int) -> float:
	var worst := 0.0
	var _log: Array = p.divergence_log
	var start: int = maxi(0, _log.size() - n)
	for i in range(start, _log.size()):
		worst = maxf(worst, float(_log[i]["divergence"]))
	return worst
