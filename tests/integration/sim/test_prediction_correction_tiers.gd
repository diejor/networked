## The physics correction tier: SNAP_BLEND spring, collision cooldown, body sleep,
## per-field trigger and restore participation, angle-aware compare, the
## pose_corrected display delta, the consume drain, and the de-jitter buffer.
##
## These are the reconciliation features a dynamic-body game needs on top of the
## bare SNAP restore. SNAP_BLEND eases a sub-threshold correction onto the
## authoritative pose over several ticks (or in one reported write at full
## stiffness) and flushes its residual so it genuinely lands; a contact or a
## sleeping body pauses it; a reconcile-only field reconciles without triggering
## and a teleport-only field is never rewound below the teleport tier; the
## consume drain lets the server catch up after a hitch while the consume buffer
## keeps de-jitter slack standing instead of draining to empty.
class_name TestPredictionCorrectionTiers
extends NetwTestSuite

const SNAP := PredictionComponent.CorrectionMode.SNAP
const SNAP_BLEND := PredictionComponent.CorrectionMode.SNAP_BLEND
const RIGHT := { &"motion": Vector2.RIGHT, &"bombing": false }


# --- F5: per-field correction participation ---

# A field named in the excludes set never triggers a correction on its own, so a
# drift confined to it leaves the trigger quiet. A drift on any other field still
# fires. This is the trigger math the reconcile-only fields ride.
func test_excluded_field_does_not_trigger_but_others_do() -> void:
	var predicted := { &"position": Vector2(1, 0), &"heading": 0.0 }
	var only_heading := { &"position": Vector2(1, 0), &"heading": 1.5 }
	var also_position := { &"position": Vector2(9, 0), &"heading": 1.5 }
	var excludes := { &"heading": true }

	assert_bool(NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted, only_heading, 0.01, { }, excludes,
	)).override_failure_message(
		"an excluded field's drift alone must not trigger a correction",
	).is_false()
	assert_bool(NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted, also_position, 0.01, { }, excludes,
	)).override_failure_message(
		"a non-excluded field's drift must still trigger",
	).is_true()
	# With no excludes the heading drift triggers as before.
	assert_bool(NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted, only_heading, 0.01, { }, { },
	)).is_true()


func test_prediction_component_maps_reconcile_only_fields() -> void:
	var pred: PredictionComponent = auto_free(PredictionComponent.new())
	pred.reconcile_only_fields = [&"heading", &"linear_speed"]
	pred.teleport_only_restore_fields = [&"velocity"]
	pred.consume_buffer_ticks = 2
	var handle := NetwLagCompensationInterface.PredictionHandle.new()
	pred._push_config(handle)
	assert_bool(handle.correction_trigger_excludes.has(&"heading")).is_true()
	assert_bool(handle.correction_trigger_excludes.has(&"linear_speed")).is_true()
	assert_bool(handle.correction_trigger_excludes.has(&"position")).is_false()
	assert_bool(handle.teleport_only_restore.has(&"velocity")).is_true()
	assert_int(handle.consume_buffer_ticks).is_equal(2)


# An angle-flagged field compares by its shortest arc, so a heading crossing the
# ±π wrap reads its true error instead of a near-full turn that would fire a
# spurious correction every time the body turns around.
func test_diverged_compares_angle_fields_across_the_wrap() -> void:
	var predicted := { &"heading": 3.1 }
	var authoritative := { &"heading": -3.1 }
	assert_bool(NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted, authoritative, 0.2, { }, { }, { },
	)).override_failure_message(
		"a plain float compare across the wrap reads ~6.2 and must trigger",
	).is_true()
	assert_bool(NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted, authoritative, 0.2, { }, { }, { &"heading": true },
	)).override_failure_message(
		"an angle compare across the wrap reads the ~0.08 arc and must stay quiet",
	).is_false()


# --- Restore participation: teleport-only fields ---

# A field in teleport_only_restore survives a sub-threshold correction untouched
# on the predicted body, while an unmarked field is rewound to the authoritative
# value. A teleport-tier correction restores both.
func test_teleport_only_restore_preserved_below_teleport_tier() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompMomentumBody
	await s.setup(self)
	var kept := await s.add_predicted_entity([&"position", &"velocity", &"boost"])
	var rewound := await s.add_predicted_entity([&"position", &"velocity", &"boost"])
	var excludes: Dictionary[StringName, bool] = { &"boost": true }
	for p: PredictedEntity in [kept, rewound]:
		p.client_prediction.correction_mode = SNAP_BLEND
		p.client_prediction.teleport_threshold = 2.0
		p.client_prediction.correction_trigger_excludes = excludes
	var teleport_only: Dictionary[StringName, bool] = { &"boost": true }
	kept.client_prediction.teleport_only_restore = teleport_only

	s.latency_both(4)
	s.hold_input(kept, RIGHT)
	s.hold_input(rewound, RIGHT)
	s.run(20)
	kept.server_root.boost = 7.0
	rewound.server_root.boost = 7.0
	# A sub-threshold position nudge triggers a correction on both entities.
	s.perturb_server(kept, Vector2(0.0, -1.0))
	s.perturb_server(rewound, Vector2(0.0, -1.0))
	s.run(30)
	assert_float(rewound.client_root.boost) \
		.override_failure_message(
			"an unmarked field must rewind to the authoritative value",
		).is_equal_approx(7.0, 0.01)
	assert_float(kept.client_root.boost) \
		.override_failure_message(
			"a teleport-only field must survive a sub-threshold correction",
		).is_equal_approx(0.0, 0.01)
	# A teleport-tier desync restores even the teleport-only field.
	s.perturb_server(kept, Vector2(0.0, -40.0))
	s.run(30)
	assert_float(kept.client_root.boost) \
		.override_failure_message(
			"a teleport must restore the full state, teleport-only included",
		).is_equal_approx(7.0, 0.01)


# --- pose_corrected: the display-absorption delta ---

# At full stiffness a sub-threshold correction lands in one write and reports the
# pose change it applied, so display code can absorb it into a decaying render
# offset. A teleport-tier correction reports teleported so the display snaps.
func test_pose_corrected_reports_one_write_corrections() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var p := await s.add_predicted_entity([&"position", &"velocity"])
	p.client_prediction.correction_mode = SNAP_BLEND
	p.client_prediction.blend_stiffness = 1.0
	p.client_prediction.teleport_threshold = 2.0
	var events: Array[Dictionary] = []
	p.client_prediction.pose_corrected.connect(
		func(deltas: Dictionary, teleported: bool) -> void:
			events.append({ &"deltas": deltas, &"teleported": teleported }),
	)

	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.warmup(p, 20)
	events.clear()
	s.perturb_server(p, Vector2(0.0, -1.0))
	s.run(30)
	assert_bool(events.is_empty()) \
		.override_failure_message("a correction that moved the body must report").is_false()
	var first: Dictionary = events[0]
	assert_bool(first[&"teleported"]).is_false()
	var delta: Vector2 = first[&"deltas"].get(&"position", Vector2.ZERO)
	assert_float(delta.y) \
		.override_failure_message(
			"the reported delta must match the applied correction, got %.3f" % delta.y,
		).is_equal_approx(-1.0, 0.3)

	events.clear()
	s.perturb_server(p, Vector2(0.0, -40.0))
	s.run(30)
	var saw_teleport := false
	for e: Dictionary in events:
		if e[&"teleported"]:
			saw_teleport = true
	assert_bool(saw_teleport) \
		.override_failure_message("a teleport-tier correction must report teleported") \
		.is_true()


# The spring flushes its residual on the final blend tick instead of abandoning
# it, so a correction genuinely lands even when the leftover sits below the
# divergence band and no follow-up correction would ever re-aim it.
func test_snap_blend_lands_within_blend_ticks() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP_BLEND
	blend.client_prediction.blend_stiffness = 0.2
	blend.client_prediction.blend_ticks = 8
	# A band wider than the abandoned tail would be: without the flush the body
	# parks (1-s)^N ≈ 0.17 off truth and nothing ever re-corrects it.
	var overrides: Dictionary[StringName, float] = { &"position": 0.5 }
	blend.client_prediction.divergence_epsilon_overrides = overrides

	s.latency_both(4)
	s.hold_input(blend, RIGHT)
	s.run(20)
	s.perturb_server(blend, Vector2(0.0, -1.0))
	s.run(40)
	assert_float(blend.client_body.position.y) \
		.override_failure_message(
			"the blend must land on -1.0, not park at its abandoned tail (y=%.3f)"
			% blend.client_body.position.y,
		).is_equal_approx(-1.0, 0.05)


# --- F3: restore-age clamp ---

func test_max_restore_ticks_default_and_push() -> void:
	var pred: PredictionComponent = auto_free(PredictionComponent.new())
	assert_int(pred.max_restore_ticks).is_equal(6)
	pred.max_restore_ticks = 3
	var handle := NetwLagCompensationInterface.PredictionHandle.new()
	pred._push_config(handle)
	assert_int(handle.max_restore_ticks).is_equal(3)


# --- F4: SNAP_BLEND spring ---

# A sub-threshold correction eases the transverse offset onto the body over several
# ticks, so no single tick teleports the whole error. A plain SNAP restore closes
# the same offset in one tick, so its largest single-tick move is the full error
# while the blend's is a fraction of it. Both converge onto the authoritative offset.
func test_snap_blend_eases_instead_of_teleporting() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var snap := await s.add_predicted_entity([&"position", &"velocity"])
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	snap.client_prediction.correction_mode = SNAP
	blend.client_prediction.correction_mode = SNAP_BLEND
	blend.client_prediction.blend_stiffness = 0.2

	s.latency_both(4)
	s.hold_input(snap, RIGHT)
	s.hold_input(blend, RIGHT)
	s.run(20)
	# A sub-threshold transverse nudge (well under teleport_threshold 2.0).
	s.perturb_server(snap, Vector2(0.0, -1.0))
	s.perturb_server(blend, Vector2(0.0, -1.0))
	var snap_ys: Array[float] = []
	var blend_ys: Array[float] = []
	for _i in range(40):
		s.run(1)
		snap_ys.append(snap.client_body.position.y)
		blend_ys.append(blend.client_body.position.y)

	var snap_jump := _max_abs_jump(snap_ys)
	var blend_jump := _max_abs_jump(blend_ys)
	assert_float(snap_jump) \
		.override_failure_message(
			"SNAP should teleport the ~1.0 offset in one tick, biggest jump was %.3f" % snap_jump,
		).is_greater(0.5)
	assert_float(blend_jump) \
		.override_failure_message(
			"SNAP_BLEND should ease (biggest single-tick move %.3f), never teleport the offset" % blend_jump,
		).is_less(0.5)
	assert_float(blend_jump).is_less(snap_jump)
	# Both converge onto the authoritative transverse offset.
	assert_float(blend.client_body.position.y) \
		.override_failure_message(
			"blend body y %.3f did not converge to authoritative -1.0" % blend.client_body.position.y,
		).is_equal_approx(-1.0, 0.2)


# A correction past teleport_threshold abandons the spring and hard-snaps, so a big
# desync closes in a single tick instead of dragging the body through the world.
func test_snap_blend_teleports_past_threshold() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP_BLEND
	blend.client_prediction.teleport_threshold = 2.0

	s.latency_both(4)
	s.hold_input(blend, RIGHT)
	s.run(20)
	# Well past the threshold: a genuine desync that must not be eased through.
	s.perturb_server(blend, Vector2(0.0, -40.0))
	var ys: Array[float] = []
	for _i in range(20):
		s.run(1)
		ys.append(blend.client_body.position.y)

	# The correction lands as one large jump, not a slow spring.
	assert_float(_max_abs_jump(ys)) \
		.override_failure_message("a teleport should move the body in one big jump") \
		.is_greater(10.0)
	assert_float(blend.client_body.position.y) \
		.override_failure_message(
			"teleport did not land near authoritative -40.0, y=%.2f" % blend.client_body.position.y,
		).is_equal_approx(-40.0, 0.5)


# A contact cooldown pauses the spring: a sub-threshold divergence inside the window
# leaves the body on its predicted path, then converges once the window passes.
func test_collision_cooldown_pauses_the_spring() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP_BLEND
	blend.client_prediction.collision_cooldown_ticks = 30

	s.latency_both(4)
	s.hold_input(blend, RIGHT)
	s.run(20)
	s.reset_metrics(blend)
	blend.client_prediction.notify_contact()
	s.perturb_server(blend, Vector2(0.0, -1.0))
	s.run(12)
	# Inside the cooldown the body was never sprung toward the authoritative offset.
	assert_float(blend.client_body.position.y) \
		.override_failure_message(
			"cooldown should have held y near 0, got %.3f" % blend.client_body.position.y,
		).is_equal_approx(0.0, 0.05)
	# Past the window the spring resumes and converges.
	s.run(40)
	assert_float(blend.client_body.position.y).is_equal_approx(-1.0, 0.2)


# A sleeping body is never nudged by reconciliation: a sub-threshold divergence
# holds while asleep, then converges after it wakes.
func test_sleeping_body_pauses_the_spring() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP_BLEND

	s.latency_both(4)
	s.hold_input(blend, RIGHT)
	s.run(20)
	s.reset_metrics(blend)
	blend.client_prediction.sleeping = true
	s.perturb_server(blend, Vector2(0.0, -1.0))
	s.run(12)
	assert_float(blend.client_body.position.y) \
		.override_failure_message(
			"a sleeping body should not be sprung, got y=%.3f" % blend.client_body.position.y,
		).is_equal_approx(0.0, 0.05)
	blend.client_prediction.sleeping = false
	s.run(40)
	assert_float(blend.client_body.position.y).is_equal_approx(-1.0, 0.2)


# --- F2: consume drain + ack-age surface ---

# With a queued backlog the server drains up to max_consume_per_tick present inputs
# in one tick, so the ack catches up instead of ratcheting one-behind forever. The
# default budget of 1 consumes exactly one.
func test_consume_drain_catches_up_on_backlog() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var one := await s.add_predicted_entity()
	var many := await s.add_predicted_entity()
	one.server_prediction.max_consume_per_tick = 1
	many.server_prediction.max_consume_per_tick = 3

	# Queue a ten-tick backlog of present inputs on each server timeline.
	for tick in range(10):
		s.feed_server_input(one, tick, RIGHT)
		s.feed_server_input(many, tick, RIGHT)

	s.consume_step(one, 0)
	s.consume_step(many, 0)

	assert_int(one.consumed) \
		.override_failure_message("budget 1 consumed %d" % one.consumed).is_equal(1)
	assert_int(many.consumed) \
		.override_failure_message("budget 3 should drain 3, got %d" % many.consumed).is_equal(3)
	# ack_age_ticks surfaces the remaining backlog: newest input (9) minus the ack.
	# Budget 3 drained through tick 2 (age 7); budget 1 stopped at tick 0 (age 9).
	assert_int(many.server_prediction.ack_age_ticks).is_equal(7)
	assert_int(one.server_prediction.ack_age_ticks).is_equal(9)


# The consume buffer holds the very first consume until the queue spans past it,
# then keeps that slack standing: steady arrivals consume one per tick at a
# constant ack age, and a burst drains back down to the target, never to empty.
# The slack is what absorbs input-arrival phase drift so the authoritative body
# never alternates starved and doubled ticks.
func test_consume_buffer_warms_up_then_holds_slack() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	p.server_prediction.consume_buffer_ticks = 2
	p.server_prediction.max_consume_per_tick = 3

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

	# A four-tick burst drains back to the buffer target, never to empty.
	for tick in range(9, 13):
		s.feed_server_input(p, tick, RIGHT)
	s.consume_step(p, 13)
	s.consume_step(p, 14)
	assert_int(p.server_prediction.ack_age_ticks) \
		.override_failure_message(
			"a burst must drain to the buffer target, not to empty",
		).is_equal(2)


# A state frame is only ever a truthful (tick, ack, payload) triple. The payload
# is gathered live when the pump flushes, so a tick that consumed no input must
# author nothing: re-stamping would ship a body that has coasted past the ack
# under that same ack, and the owning client reads that gap as its own divergence.
func test_starved_consume_tick_authors_no_state_frame() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()

	s.feed_server_input(p, 0, RIGHT)
	s.consume_step(p, 0)
	s.record_server_history(p, 0)
	assert_int(p.consumed).is_equal(1)
	assert_bool(p.server_state.suppress_volatile) \
		.override_failure_message("a consuming tick must author its frame") \
		.is_false()
	var authored := p.server_state.authored_tick
	var acked := p.server_state.reconcile_ack
	var settled: Variant = s.server_state_at(p, acked + 1).get(&"position")

	# The next tick has nothing queued. The body still drifts (a solver coasts,
	# a closed-form body is nudged here), but the ack cannot move with it.
	s.perturb_server(p, Vector2(5.0, 0.0))
	s.consume_step(p, 1)
	s.record_server_history(p, 1)

	assert_int(p.starved) \
		.override_failure_message("an empty queue must count as starved").is_equal(1)
	assert_bool(p.server_state.suppress_volatile) \
		.override_failure_message("a starved tick must send no volatile row") \
		.is_true()
	assert_int(p.server_state.authored_tick) \
		.override_failure_message("a starved tick must not re-stamp the frame tick") \
		.is_equal(authored)
	assert_int(p.server_state.reconcile_ack).is_equal(acked)
	assert_that(s.server_state_at(p, acked + 1).get(&"position")) \
		.override_failure_message(
			"the ack's history slot must keep the state its consume produced",
		).is_equal(settled)

	# The next real arrival authors again, carrying the advanced ack.
	s.feed_server_input(p, 1, RIGHT)
	s.consume_step(p, 2)
	assert_bool(p.server_state.suppress_volatile).is_false()
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
# A field that drives the simulation but has no projectable derivative cannot be
# restored outright: the ack-tick value is wrong by the time it lands, so the
# write forks the body again. The pull closes the same error in small steps, so
# the error falls monotonically and no single tick shows a jump.
func test_soft_restore_converges_without_a_snap() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var handle := p.client_prediction
	handle.soft_restore_stiffness = { &"position": 0.1 }

	var engine = handle._engine()
	engine._soft_target = { &"position": Vector2(10.0, 0.0) }
	p.client_body.position = Vector2.ZERO

	var samples: Array[float] = []
	for i in range(40):
		engine._apply_soft_pull()
		samples.append(p.client_body.position.x)

	# Monotone approach: every step closes error and none overshoots.
	for i in range(1, samples.size()):
		assert_float(samples[i]) \
			.override_failure_message(
				"step %d moved backwards: %.4f -> %.4f" % [i, samples[i - 1], samples[i]],
			).is_greater_equal(samples[i - 1])
	assert_float(samples[-1]) \
		.override_failure_message("the pull must converge on authority") \
		.is_equal_approx(10.0, 0.2)

	# No step is a snap: the largest single move is the first, one stiffness of
	# the error, not the whole of it.
	var biggest := 0.0
	var previous := 0.0
	for value in samples:
		biggest = maxf(biggest, absf(value - previous))
		previous = value
	assert_float(biggest) \
		.override_failure_message(
			"largest single step %.4f should be about one stiffness of 10.0" % biggest,
		).is_less(1.5)


# A field left out of the soft set is untouched by the pull, so the knob never
# reaches a field the recipe did not name.
func test_soft_restore_only_touches_its_own_fields() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var handle := p.client_prediction
	handle.soft_restore_stiffness = { }

	var engine = handle._engine()
	engine._soft_target = { &"position": Vector2(10.0, 0.0) }
	p.client_body.position = Vector2.ZERO
	for i in range(10):
		engine._apply_soft_pull()

	assert_vector(p.client_body.position) \
		.override_failure_message("an unlisted field must not be pulled") \
		.is_equal(Vector2.ZERO)


# The largest absolute step between consecutive samples, the per-tick jump a
# teleport produces and an eased spring never does.
func _max_abs_jump(samples: Array[float]) -> float:
	var worst := 0.0
	for i in range(1, samples.size()):
		worst = maxf(worst, absf(samples[i] - samples[i - 1]))
	return worst
