## Real-node ack-based reconciliation (ports the lag-comp spike tier C).
##
## Drives the production [StateSynchronizer], [InputSynchronizer], and
## [PredictionComponent] across a real loopback through [PredictionScenario]: the
## owning client predicts immediately, the server stamps an ack, and the client
## reconciles on that ack with no clock lead and no determinism contract. This is
## the headline lag-comp claim, now proven on the shipping nodes instead of the
## spike doubles.
class_name TestPredictionReconciliation
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }


func test_clean_predicts_authority_with_no_corrections() -> void:
	# Exact, lossless delivery: once the link settles, the client's prediction
	# matches the server's authoritative computation tick for tick, so no
	# correction ever fires in steady state.
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.warmup(p)
	s.run(70)

	assert_int(p.consumed).is_greater(10)
	assert_int(p.observer.divergence_log.size()).is_greater(5)
	assert_int(p.corrections).is_equal(0)
	assert_float(p.client_body.position.x).is_greater(0.0)
	assert_float(p.server_body.position.x).is_greater(0.0)


func test_perturbation_reconverges() -> void:
	# A server-only nudge the client never predicted forces a divergence. The
	# client must snap to authority, replay its pending inputs, and reconverge.
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(30)
	s.perturb_server(p, Vector2(60.0, -40.0))
	s.run(70)

	assert_int(p.corrections).is_greater_equal(1)
	# Steady state after the correction: divergence back under epsilon.
	assert_float(p.tail_divergence(5)).is_less(p.epsilon)


func test_replay_depth_is_bounded() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(30)
	s.perturb_server(p, Vector2(80.0, 0.0))
	s.run(60)

	# Replay walks only the in-flight window (about RTT in ticks), never spirals.
	assert_int(p.max_replay_depth).is_greater(0)
	assert_int(p.max_replay_depth).is_less(30)


func test_fire_fires_once_despite_replays() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)

	# Pulse the fire for exactly one predict tick, then perturb just past it so a
	# correction replays across the fire edge.
	var fire_tick := s.client_clock.tick + 25
	s.set_input_at(p, fire_tick, { &"motion": Vector2.RIGHT, &"bombing": true })
	s.run(27)
	s.perturb_server(p, Vector2(50.0, 50.0))
	s.run(50)

	# The predictor fired on the fresh pass only; replays (is_fresh=false) never
	# re-fire. The server consumed the fire input exactly once.
	assert_int(p.fire_count).is_equal(1)
	assert_int(p.server_fire_count).is_equal(1)
	assert_int(p.corrections).is_greater_equal(1)


func test_stall_policy_skips_lost_input() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
	)
	# Ticks 0 and 2 move; the lost tick 1 contributes nothing.
	_consume_with_hole(s, p)
	var step := SpikeSim.SPEED * s.dt()
	assert_that(p.server_body.position.x).is_equal_approx(2.0 * step, 0.001)
	assert_int(p.missing).is_equal(1)


func test_repeat_last_policy_carries_input() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.REPEAT_LAST,
	)
	# The lost tick repeats the prior input, so all three ticks move.
	_consume_with_hole(s, p)
	var step := SpikeSim.SPEED * s.dt()
	assert_that(p.server_body.position.x).is_equal_approx(3.0 * step, 0.001)
	assert_int(p.missing).is_equal(1)


# Feeds input at ticks 0 and 2 (skipping 1) and consumes ticks 0..2 on the
# server, so the missing-input policy decides what tick 1 does.
func _consume_with_hole(s: PredictionScenario, p: PredictedEntity) -> void:
	s.feed_server_input(p, 0, RIGHT)
	s.feed_server_input(p, 2, RIGHT)
	s.consume_step(p, 0)
	s.consume_step(p, 1)
	s.consume_step(p, 2)
