## The physics recovery tiers: which fields a correction's one write covers, when
## it declines to happen at all, and what it reports having done.
##
## These are the reconciliation features a dynamic-body game needs on top of the
## bare restore. A recovery is staged and applied as one write, so what these
## laws pin is its reach and its silence: a contact or a sleeping body pauses a
## sub-teleport recovery, a reconcile-only field reconciles without triggering,
## a teleport-only field is never rewound below the teleport tier, and the
## teleport tier overrides both. When authority consumes the transitions these
## corrections judge is [TestPredictConsumeTiers].
class_name TestPredictionCorrectionTiers
extends NetwTestSuite

const SNAP := PredictionComponent.CorrectionMode.SNAP
const EXTRAPOLATED := PredictionComponent.RestoreMode.EXTRAPOLATED
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


# The per-field recovery facts are property marks, so the derived state set is
# where the engine reads them, while the component pushes entity-level policy
# only. One fact, one source on each side of that line.
func test_field_recovery_facts_ride_the_marks() -> void:
	var probe: LagCompWithheldBody = auto_free(LagCompWithheldBody.new())
	var set := NetwSyncSet.from_script(
		probe.get_script() as Script,
		NetwSyncSet.Record.RECORD_STATE,
	)
	var boost: NetwSyncSet.Field = null
	var position: NetwSyncSet.Field = null
	for field: NetwSyncSet.Field in set.fields:
		if field.key == &"boost":
			boost = field
		elif field.key == &"position":
			position = field
	assert_that(boost).is_not_null()
	assert_bool(boost.explicit_teleport_only).override_failure_message(
		"the teleport_only() mark must reach the derived set",
	).is_true()
	assert_that(position).is_not_null()
	assert_bool(position.explicit_teleport_only).is_false()

	var pred: PredictionComponent = auto_free(PredictionComponent.new())
	pred.consume_buffer_ticks = 2
	var handle := NetwLagCompensationInterface.PredictionHandle.new()
	pred._push_config(handle)
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
# value. Partiality is an in-domain refinement, so the rig declares each
# entity's island; an undeclared entity restores the whole closure instead. A
# teleport-tier correction restores both fields, and a withheld field that
# never re-converges is eventually restored by escalation anyway
# ([TestPredictRecoveryLaws]), so the survival is asserted at the first
# correction rather than at a steady state that no longer exists.
func test_teleport_only_restore_preserved_below_teleport_tier() -> void:
	var s := PredictionScenario.new()
	# The withheld set is the teleport_only() mark, so the contrast is two body
	# declarations: the marked twin withholds, the unmarked one restores.
	s.body_type = LagCompWithheldBody
	var kept: PredictedEntity
	var rewound: PredictedEntity
	await s.setup(self)
	kept = await s.add_predicted_entity([&"position", &"velocity", &"boost"])
	s.body_type = LagCompMomentumBody
	rewound = await s.add_predicted_entity([&"position", &"velocity", &"boost"])
	for p: PredictedEntity in [kept, rewound]:
		p.client_prediction.correction_mode = SNAP
		p.client_prediction.snap_restore = EXTRAPOLATED
		p.client_prediction.teleport_threshold = 2.0
	kept.client_prediction.configure_island({
		participants = [rewound.client_entity],
	})
	rewound.client_prediction.configure_island({
		participants = [kept.client_entity],
	})

	s.latency_both(4)
	s.hold_input(kept, RIGHT)
	s.hold_input(rewound, RIGHT)
	s.run(20)
	s.reset_metrics(kept)
	s.reset_metrics(rewound)
	kept.server_root.boost = 7.0
	rewound.server_root.boost = 7.0
	s.run_until(
		func() -> bool:
			return kept.observer.correction_count >= 1 \
					and rewound.observer.correction_count >= 1,
	)
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

# A sub-teleport recovery lands in one write and reports the pose change it
# applied, so display code can absorb it into a decaying render offset. A
# teleport-tier recovery reports teleported so the display snaps.
func test_recovered_reports_one_write_corrections() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var p := await s.add_predicted_entity([&"position", &"velocity"])
	p.client_prediction.correction_mode = SNAP
	p.client_prediction.snap_restore = EXTRAPOLATED
	p.client_prediction.teleport_threshold = 2.0
	var events: Array[Dictionary] = []
	p.client_prediction.recovered.connect(
		func(_entry: int, deltas: Dictionary, teleported: bool, _attribution: int) -> void:
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


# --- the superseding recovery signals ---

# A divergence is reported before anything is done about it, so a game that only
# observes hears about one exactly as loudly as a game that corrects.
func test_a_divergence_is_reported_before_it_is_recovered() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var p := await s.add_predicted_entity([&"position", &"velocity"])
	p.client_prediction.correction_mode = SNAP
	p.client_prediction.snap_restore = EXTRAPOLATED
	p.client_prediction.teleport_threshold = 2.0
	var order: Array[String] = []
	p.client_prediction.divergence_detected.connect(
		func(_entry: int, _attribution: int) -> void:
			order.append("detected"),
	)
	p.client_prediction.recovered.connect(
		func(_e: int, _d: Dictionary, _t: bool, _a: int) -> void:
			order.append("recovered"),
	)

	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.warmup(p, 20)
	order.clear()
	s.perturb_server(p, Vector2(0.0, -1.0))
	s.run(30)

	assert_bool(order.is_empty()) \
		.override_failure_message("a divergence must be reported").is_false()
	assert_str(order[0]).override_failure_message(
		"the divergence must be reported before the recovery that answers it",
	).is_equal("detected")


# --- F3: restore-age clamp ---

func test_max_restore_ticks_default_and_push() -> void:
	var pred: PredictionComponent = auto_free(PredictionComponent.new())
	assert_int(pred.max_restore_ticks).is_equal(6)
	pred.max_restore_ticks = 3
	var handle := NetwLagCompensationInterface.PredictionHandle.new()
	pred._push_config(handle)
	assert_int(handle.max_restore_ticks).is_equal(3)




# A recovery past teleport_threshold restores the whole closure, so a big desync
# closes in a single tick instead of dragging the body through the world.
func test_a_recovery_past_the_threshold_teleports() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP
	blend.client_prediction.snap_restore = EXTRAPOLATED
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


# A contact cooldown pauses the recovery: a sub-threshold divergence inside the
# window leaves the body on its predicted path, then converges once the window
# passes.
func test_collision_cooldown_pauses_the_recovery() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP
	blend.client_prediction.snap_restore = EXTRAPOLATED
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
func test_sleeping_body_pauses_the_recovery() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompForecastBody
	await s.setup(self)
	var blend := await s.add_predicted_entity([&"position", &"velocity"])
	blend.client_prediction.correction_mode = SNAP
	blend.client_prediction.snap_restore = EXTRAPOLATED

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


# The largest absolute step between consecutive samples, the per-tick jump a
# teleport produces and an eased spring never does.
func _max_abs_jump(samples: Array[float]) -> float:
	var worst := 0.0
	for i in range(1, samples.size()):
		worst = maxf(worst, absf(samples[i] - samples[i - 1]))
	return worst
