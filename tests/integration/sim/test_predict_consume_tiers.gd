## The consume tier: the standing de-jitter buffer, the starved tick's silence,
## the stranded-cursor resync, and the ack-age surface they are all read through.
##
## Authority consumes one queued transition per tick, so these laws are about
## WHEN it consumes rather than what it produces: how much slack it keeps
## standing against input-arrival phase drift, what it does with a tick that has
## nothing to run, and how it recovers a cursor that has fallen too far behind to
## walk back. What a divergence then does with the result is
## [TestPredictionCorrectionTiers].
class_name TestPredictConsumeTiers
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT, &"bombing": false }


# --- F2: the standing consume buffer ---


# The consume buffer holds the very first consume until the queue spans past it,
# then keeps that slack standing: steady arrivals consume one per tick at a
# constant ack age. The slack is what absorbs input-arrival phase drift so the
# authoritative body never alternates starved and doubled ticks.
func test_consume_buffer_warms_up_then_holds_slack() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	p.server_prediction.consume_buffer_ticks = 2

	# Warm-up: consumption holds, without counting missing ticks, until the
	# queued span exceeds the buffer.
	s.feed_server_input(p, 0, RIGHT)
	s.consume_step(p, 0)
	s.feed_server_input(p, 1, RIGHT)
	s.consume_step(p, 1)
	assert_int(p.consumed) \
		.override_failure_message("the cursor must hold until the buffer fills") \
		.is_equal(0)
	assert_int(p.missing).is_equal(0)
	s.feed_server_input(p, 2, RIGHT)
	s.consume_step(p, 2)
	assert_int(p.consumed).is_equal(1)

	# Steady one-per-tick arrivals hold the slack at the buffer depth.
	for tick in range(3, 9):
		s.feed_server_input(p, tick, RIGHT)
		s.consume_step(p, tick)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message(
			"steady arrivals must hold ack age at the buffer depth",
		).is_equal(2)

	# A burst is worked off at the tick rate, one input per tick, so the ack never
	# jumps a span no single tick produced. Four queued arrivals and two consume
	# ticks close exactly two of them.
	var before_burst: int = p.server_prediction.ack_age_ticks
	for tick in range(9, 13):
		s.feed_server_input(p, tick, RIGHT)
	s.consume_step(p, 13)
	s.consume_step(p, 14)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message(
			"two ticks must close exactly two of the four queued inputs",
		).is_equal(before_burst + 4 - 2)


# A held authority tick still owes every remote observer its state, so the
# frame flows on every tick. What it must not do is acknowledge: the body has
# coasted past the ack, and re-stamping the old ack against the coasted payload
# would frame the coast as divergence the owning client did not cause.
func test_starved_consume_tick_ships_state_that_acks_nothing() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()

	s.feed_server_input(p, 0, RIGHT)
	s.consume_step(p, 0)
	s.record_server_history(p, 0)
	assert_int(p.consumed).is_equal(1)
	var acked := p.server_state.reconcile_ack
	assert_int(acked).is_greater_equal(0)
	var settled: Variant = s.server_state_at(p, acked + 1).get(&"position")

	# The next tick has nothing queued. The body still drifts (a solver coasts,
	# a closed-form body is nudged here), but the ack cannot move with it.
	s.perturb_server(p, Vector2(5.0, 0.0))
	s.consume_step(p, 1)
	s.record_server_history(p, 1)

	assert_int(p.starved) \
		.override_failure_message("an empty queue must count as starved").is_equal(1)
	assert_int(p.server_state.authored_tick) \
		.override_failure_message(
			"a starved tick still frames its state for remote observers",
		).is_equal(1)
	assert_int(p.server_state.reconcile_ack) \
		.override_failure_message("a starved tick must acknowledge nothing") \
		.is_equal(-1)
	assert_that(s.server_state_at(p, acked + 1).get(&"position")) \
		.override_failure_message(
			"the ack's history slot must keep the state its consume produced",
		).is_equal(settled)

	# The next real arrival acknowledges again, carrying the advanced ack.
	s.feed_server_input(p, 1, RIGHT)
	s.consume_step(p, 2)
	assert_int(p.server_state.reconcile_ack).is_equal(acked + 1)


# The de-jitter depth is maintained, not warmed up once. A tick that would drop
# the queue below the target holds and rebuilds the slack instead of spending it,
# so a single arrival slip costs one delayed input rather than starving a tick
# and leaving the cursor with no slack for the next slip.
func test_consume_depth_self_restores_after_a_slip() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	p.server_prediction.consume_buffer_ticks = 2
	p.server_prediction.max_consume_per_tick = 3

	for tick in range(3):
		s.feed_server_input(p, tick, RIGHT)
	s.consume_step(p, 0)
	for tick in range(3, 8):
		s.feed_server_input(p, tick, RIGHT)
		s.consume_step(p, tick)
	assert_int(p.server_prediction.ack_age_ticks).is_equal(2)
	s.reset_metrics(p)

	# One tick where no input arrives. Consuming would spend the standing depth
	# and leave the next slip to starve, so the tick holds instead and the depth
	# survives the slip intact. One input is delayed by one solver step.
	s.consume_step(p, 8)
	assert_int(p.held) \
		.override_failure_message("a slip inside the buffer must hold, not starve") \
		.is_equal(1)
	assert_int(p.starved).is_equal(0)
	assert_int(p.consumed).is_equal(0)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message("a hold must preserve the standing depth") \
		.is_equal(2)

	# Arrivals resume, the catch-up pair included. Nothing was dropped: every
	# delayed input is consumed in order behind the standing buffer, and the
	# depth settles on the drain floor of buffer + 1.
	s.feed_server_input(p, 8, RIGHT)
	s.feed_server_input(p, 9, RIGHT)
	s.consume_step(p, 9)
	for tick in range(10, 12):
		s.feed_server_input(p, tick, RIGHT)
		s.consume_step(p, tick)

	assert_int(p.consumed) \
		.override_failure_message("no input may be dropped across a slip") \
		.is_equal(3)
	assert_int(p.starved) \
		.override_failure_message("a repaid slip must never starve a tick") \
		.is_equal(0)
	assert_int(p.held).is_equal(1)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message("the depth must never fall below the target") \
		.is_equal(3)


# A client that joins a running session anchors its clock after it has already
# authored input, so the server's cursor opens on ticks the controller has left
# far behind. The cursor cannot walk to the live edge (it heals one absent tick
# per server tick while the controller authors one), so past the lag ceiling it
# re-opens at the edge instead. Without this the entity simulates forever without
# ever reconciling, which reads as a car that drives but never corrects.
func test_stranded_consume_cursor_resyncs_to_the_live_edge() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	p.server_prediction.consume_buffer_ticks = 1
	p.server_prediction.max_consume_lag_ticks = 60

	# The pre-anchor inputs open the cursor down at tick 5.
	for tick in range(5, 9):
		s.feed_server_input(p, tick, RIGHT)
	s.consume_step(p, 0)
	assert_int(p.server_prediction.resync_count).is_equal(0)

	# The clock anchors and the controller resumes authoring a thousand ticks up.
	for tick in range(1000, 1004):
		s.feed_server_input(p, tick, RIGHT)
	s.reset_metrics(p)
	s.consume_step(p, 1)

	assert_int(p.server_prediction.resync_count) \
		.override_failure_message("a stranded cursor must re-open at the live edge") \
		.is_equal(1)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message(
			"after a resync the ack must sit at the buffer depth, not a thousand back",
		).is_equal(1)
	assert_int(p.missing) \
		.override_failure_message("a resync must skip the gap, not walk it as losses") \
		.is_equal(0)
	assert_int(p.server_prediction.skipped_count).is_greater(900)

	# Steady arrivals then consume in order, with no further resyncs.
	for tick in range(1004, 1010):
		s.feed_server_input(p, tick, RIGHT)
		s.consume_step(p, tick)
	assert_int(p.server_prediction.resync_count).is_equal(1)
	assert_int(p.server_prediction.ack_age_ticks).is_equal(1)


# A backlog inside the ceiling is still walked, so ordinary jitter heals in order
# and never skips input the controller expects the server to have simulated.
func test_lag_within_the_ceiling_still_walks_in_order() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	p.server_prediction.consume_buffer_ticks = 0
	p.server_prediction.max_consume_lag_ticks = 60

	for tick in range(0, 20):
		s.feed_server_input(p, tick, RIGHT)
	for tick in range(0, 20):
		s.consume_step(p, tick)

	assert_int(p.server_prediction.resync_count) \
		.override_failure_message("a 20-tick backlog is inside the ceiling") \
		.is_equal(0)
	assert_int(p.consumed) \
		.override_failure_message("every queued input must be consumed in order") \
		.is_equal(20)


# A soft-restore field converges on authority without ever being written to it.
