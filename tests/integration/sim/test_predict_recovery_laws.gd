## Laws a recovery must obey however it is configured, and the label that decides
## which comparison found it.
##
## The campaign replaced an easing spring with a single staged write, and replaced
## a tolerance with a domain label. Both replacements are only worth having if a
## recovery cannot leave a residue, cannot make the divergence it answers worse,
## and cannot apply two field rules to one field. These laws pin exactly that, so
## a later slice cannot reintroduce a tail without breaking a stated expectation.
## What a recovery STAGES is [TestPredictKernels]; which comparison FOUND it is
## [TestPredictCompareSplit].
class_name TestPredictRecoveryLaws
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const CorrectionMode := PredictionHandle.CorrectionMode
const RecoveryPolicy := PredictionHandle.RecoveryPolicy
const RestoreMode := PredictionHandle.RestoreMode
const SimMode := PredictionHandle.SimMode
const Domain := NetwPredictJournal.Domain
const Attribution := NetwPredictJournal.Attribution
const RIGHT := { &"motion": Vector2.RIGHT, &"bombing": false }


func _stage(
		payload: Dictionary,
		current: Dictionary,
		withheld: Dictionary,
		converge_rules: Dictionary,
		pose_error: float,
		domain: Domain = Domain.IN_DOMAIN,
		attribution: Attribution = Attribution.UNATTRIBUTED,
		contact_window: bool = false,
) -> Dictionary:
	return Engine_.recover(
		payload,
		RecoveryPolicy.REBASE_RECOVER,
		CorrectionMode.SNAP,
		RestoreMode.EXACT,
		{ },
		withheld,
		converge_rules,
		current,
		{ },
		pose_error,
		10.0,
		false,
		0,
		8,
		0.1,
		domain,
		attribution,
		contact_window,
	)


# --- recovery conservation ---


# The whole point of staging a recovery instead of easing one in. What the
# recovery writes IS the rebase, so the state after it is the state the journal
# will record, with nothing outstanding to be applied later.
func test_a_recovery_with_no_field_rules_stages_the_rebase_exactly() -> void:
	var payload := { &"pos": 3.0, &"vel": 7.0 }
	var plan := _stage(payload, { &"pos": 0.0, &"vel": 0.0 }, { }, { }, 1.0)
	assert_dict(plan[&"restore"]).override_failure_message(
		"a recovery with nothing declared must stage the payload untouched",
	).is_equal(payload)
	assert_dict(plan[&"write"]).is_equal(payload)


# A recovery answers a divergence, so it may never widen one. Swept across the
# rates and starting points a game can configure, because a conservation law
# that only holds at the default is not a conservation law.
func test_no_recovery_stages_a_value_further_from_authority() -> void:
	var authority := 10.0
	for rate in [0.05, 0.25, 0.5, 0.75, 1.0]:
		for start in [-40.0, -1.0, 0.0, 9.9, 10.0, 25.0]:
			var plan := _stage(
				{ &"vel": authority },
				{ &"vel": start },
				{ },
				{ &"vel": rate },
				1.0,
			)
			var before := absf(authority - start)
			var after := absf(authority - float(plan[&"restore"][&"vel"]))
			assert_float(after).override_failure_message(
				"rate %.2f from %.1f moved %.4f away from authority, was %.4f"
				% [rate, start, after, before],
			).is_less_equal(before + 0.0001)


# One field, one rule. Withholding and converging are two answers to the same
# question, and a field declared both ways must not be written by the rule that
# lost. Applying both would write a converged value the withholding was there to
# prevent, which is the failure a reader would never think to look for.
func test_a_withheld_field_is_not_resurrected_by_a_converge_rule() -> void:
	var plan := _stage(
		{ &"pos": 3.0, &"vel": 7.0 },
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ &"vel": 0.5 },
		1.0,
	)
	assert_bool(plan[&"restore"].has(&"vel")).override_failure_message(
		"a withheld field must stay withheld, not return converged",
	).is_false()
	assert_float(plan[&"restore"][&"pos"]).is_equal_approx(3.0, 0.0001)


# Past the threshold both declarations stop applying, because the predicted body
# holds nothing worth keeping. A teleport that honored either rule would leave
# the desync it exists to close only partly closed.
func test_a_teleport_honors_neither_field_rule() -> void:
	var payload := { &"pos": 3.0, &"vel": 7.0 }
	var plan := _stage(
		payload,
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ &"pos": 0.1 },
		50.0,
	)
	assert_bool(plan[&"teleport"]).is_true()
	assert_dict(plan[&"restore"]).override_failure_message(
		"a teleport must restore the whole closure verbatim",
	).is_equal(payload)


# --- the domain-keyed closure, at the kernel ---


# Partiality is an in-domain refinement. A divergence outside the declared
# domain gives no ground to decide which fields are safe to leave predicted, so
# the recovery re-bases the whole closure, withheld and converging fields
# included, without claiming the teleport tier.
func test_an_out_of_domain_recovery_writes_the_full_closure() -> void:
	var payload := { &"pos": 3.0, &"vel": 7.0 }
	var plan := _stage(
		payload,
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ &"pos": 0.5 },
		1.0,
		Domain.OUT_OF_DOMAIN,
	)
	assert_bool(plan[&"teleport"]).is_false()
	assert_bool(plan[&"skip"]).is_false()
	assert_dict(plan[&"restore"]).override_failure_message(
		"an out-of-domain recovery must restore the whole closure",
	).is_equal(payload)


# The mirror. The same declarations under the same values stay partial when the
# divergence is in domain, so the trade a game declared for its contractive
# fields survives exactly where reproducibility was declared.
func test_an_in_domain_recovery_still_honors_the_field_rules() -> void:
	var plan := _stage(
		{ &"pos": 3.0, &"vel": 7.0 },
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ &"pos": 0.5 },
		1.0,
		Domain.IN_DOMAIN,
	)
	assert_bool(plan[&"restore"].has(&"vel")).override_failure_message(
		"an in-domain recovery must still withhold the declared field",
	).is_false()
	assert_float(plan[&"restore"][&"pos"]).is_equal_approx(1.5, 0.0001)


# A divergence nobody could charge while a contact is still disturbing the
# bodies is as blind as an out-of-domain one, so the open window costs the
# recovery its partiality. A charged divergence under the same window keeps it.
func test_an_unattributed_recovery_under_an_open_window_restores_all() -> void:
	var payload := { &"pos": 3.0, &"vel": 7.0 }
	var blind := _stage(
		payload,
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ },
		1.0,
		Domain.IN_DOMAIN,
		Attribution.UNATTRIBUTED,
		true,
	)
	assert_dict(blind[&"restore"]).override_failure_message(
		"an unattributed divergence under an open contact window must restore "
		+ "the whole closure",
	).is_equal(payload)
	var charged := _stage(
		payload,
		{ &"pos": 0.0, &"vel": 0.0 },
		{ &"vel": true },
		{ },
		1.0,
		Domain.IN_DOMAIN,
		Attribution.SIMULATION,
		true,
	)
	assert_bool(charged[&"restore"].has(&"vel")).override_failure_message(
		"a charged divergence keeps its declared partiality under the window",
	).is_false()


# --- escalation, at the kernel ---


# Walks a divergence series through escalation_after the way the engine does,
# returning each recovery's escalate verdict.
func _escalations(divergences: Array, signs: Array) -> Array:
	var streak := 0
	var sign := 0
	var last := -1.0
	var out: Array = []
	for i in divergences.size():
		var v := Engine_.escalation_after(
			streak, sign, last, divergences[i], signs[i],
		)
		streak = v[&"streak"]
		sign = v[&"sign"]
		last = divergences[i]
		out.append(v[&"escalate"])
	return out


# K consecutive recoveries that never shrink the divergence promote the next
# one, so the K+1th recovery is the full closure: a bounded train instead of an
# unbounded one.
func test_a_non_shrinking_sequence_escalates_by_k_plus_one() -> void:
	assert_array(_escalations([1.0, 1.0, 1.0], [1, 1, 1])) \
			.is_equal([false, false, true])


# Two consecutive deltas that point opposite ways are overshooting each other,
# which is the oscillation signature, so the third recovery is already promoted
# rather than waiting out the full streak.
func test_alternating_sign_deltas_escalate_by_three() -> void:
	assert_array(_escalations([1.0, 1.0], [1, -1])).is_equal([false, true])


# A shrinking sequence is converging, which is what recoveries are for, so it
# must never escalate however long it runs and whatever its deltas' signs.
func test_a_shrinking_sequence_never_escalates() -> void:
	var divergences: Array = []
	var signs: Array = []
	for i in 12:
		divergences.append(1.0 / float(i + 1))
		signs.append(1 if i % 2 == 0 else -1)
	for flag in _escalations(divergences, signs):
		assert_bool(flag).override_failure_message(
			"a shrinking sequence must never escalate",
		).is_false()


# --- the projection guard, at the kernel ---


# A restore never projects a field along a channel authority disagrees about:
# that channel's own divergence is exactly the error the projection would
# multiply over the restore span. The guard is per field, so the converged
# channel keeps the landing accuracy projection buys while the diverged one
# falls back to an exact restore.
func test_a_diverged_channel_costs_only_its_own_field_the_projection() -> void:
	var guarded := Engine_.guard_projection(
		{ &"pos": &"vel", &"alt": &"alt_vel" },
		{ &"vel": 5.0, &"alt_vel": 0.001 },
		0.01,
		{ },
	)
	assert_bool(guarded.has(&"pos")).override_failure_message(
		"a field whose channel is diverged must restore exact, not project",
	).is_false()
	assert_bool(guarded.has(&"alt")).override_failure_message(
		"a converged channel must keep its projection",
	).is_true()


# The channel is judged by its own declared threshold, so a per-property
# epsilon override moves the guard with it.
func test_the_projection_guard_reads_the_channels_own_epsilon() -> void:
	var guarded := Engine_.guard_projection(
		{ &"pos": &"vel" },
		{ &"vel": 5.0 },
		0.01,
		{ &"vel": 10.0 },
	)
	assert_bool(guarded.has(&"pos")).is_true()


# --- the domain window, at the engine ---


func _island_scenario() -> Array:
	var s := PredictionScenario.new()
	await s.setup(self)
	var subject := await s.add_predicted_entity()
	var participant := await s.add_predicted_entity()
	return [s, subject, participant]


# Declaring sensors is environment attribution, not an island. An entity that
# samples the world but never names the bodies it contacts stays out of domain
# on every transition, so folding a sensor into the digest cannot ratchet a lone
# entity into a fingerprint compare it was measured never to pass.
func test_declaring_sensors_alone_leaves_every_transition_out_of_domain() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var subject := await s.add_predicted_entity()
	subject.client_prediction.configure_sensors({
		ground = func() -> float: return 1.0,
	})
	subject.client_prediction.configure_epoch(3)

	s.hold_input(subject, RIGHT)
	s.run(12)

	var domains := subject.client_prediction.journal().domains()
	assert_int(domains.size()).override_failure_message(
		"the run must have driven transitions for this law to mean anything",
	).is_greater(0)
	var in_rows := 0
	for i in domains.size():
		if domains[i] == Domain.IN_DOMAIN:
			in_rows += 1
	assert_int(in_rows).override_failure_message(
		"sensors declare the environment, not the island, so no transition may be "
		+ "in domain until configure_island names participants",
	).is_equal(0)
	await s.teardown()


# The transparency bargain has two halves: a declared sensor is folded into
# the environment digest before each drive, and the drive reads that sample
# back. The read returns what the newest drive ran against, and an undeclared
# name falls back rather than inventing a sample.
func test_a_declared_sensor_is_sampled_pre_drive_and_read_back() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var subject := await s.add_predicted_entity()
	var world: Array[int] = [0]
	subject.client_prediction.configure_sensors({
		counter = func() -> int: return world[0],
	})

	s.hold_input(subject, RIGHT)
	s.run(6, func(_tick: int) -> void: world[0] += 1)

	assert_int(int(subject.client_prediction.sensor(&"counter", -1))) \
			.override_failure_message(
				"the read-back must return the newest pre-drive sample",
			).is_equal(world[0])
	assert_int(int(subject.client_prediction.sensor(&"missing", 42))) \
			.override_failure_message(
				"an undeclared sensor reads its fallback, never an invented "
				+ "sample",
			).is_equal(42)
	await s.teardown()


# A peer whose simulation of a participant is DISPLAY is running a frozen proxy,
# so contact against it is definitionally not the contact authority resolved.
# The transitions driven inside the cooldown record that, and the ones after it
# record the entity earning its exactness back.
func test_a_contact_against_a_frozen_participant_labels_out_of_domain() -> void:
	var parts: Array = await _island_scenario()
	var s: PredictionScenario = parts[0]
	var subject: PredictedEntity = parts[1]
	var participant: PredictedEntity = parts[2]
	# The axes are what equivalence is read off, so the proxy is declared by
	# setting the axis rather than by contorting the topology into one.
	participant.client_prediction.sim_mode = SimMode.DISPLAY
	subject.client_prediction.configure_island({
		participants = [participant.client_entity],
	})
	subject.client_prediction.collision_cooldown_ticks = 4

	s.hold_input(subject, RIGHT)
	s.run(6)
	subject.client_prediction.notify_contact()
	s.run(12)

	var journal := subject.client_prediction.journal()
	var transitions := journal.transitions()
	var domains := journal.domains()
	var saw_out := false
	var saw_in_after := false
	for i in transitions.size():
		if domains[i] == Domain.OUT_OF_DOMAIN:
			saw_out = true
		elif saw_out:
			saw_in_after = true
	assert_bool(saw_out).override_failure_message(
		"a contact against a frozen proxy must open an out-of-domain window",
	).is_true()
	assert_bool(saw_in_after).override_failure_message(
		"the window must close, so a later transition earns exactness back",
	).is_true()
	await s.teardown()


# The mirror. A participant this peer simulates the way authority does is
# equivalent, so contact with it costs the entity nothing: the declaration is
# what buys the exactness back, and a test that only proved the window opens
# would pass just as well if it never closed.
func test_a_contact_against_an_equivalent_participant_keeps_exactness() -> void:
	var parts: Array = await _island_scenario()
	var s: PredictionScenario = parts[0]
	var subject: PredictedEntity = parts[1]
	var participant: PredictedEntity = parts[2]
	participant.client_prediction.sim_mode = SimMode.SPECULATIVE
	subject.client_prediction.configure_island({
		participants = [participant.client_entity],
	})

	s.hold_input(subject, RIGHT)
	s.run(6)
	subject.client_prediction.notify_contact()
	s.run(12)

	var domains := subject.client_prediction.journal().domains()
	# A journal with no rows would satisfy "no row is out of domain" without
	# having simulated anything, so the count is pinned before it is read.
	assert_int(domains.size()).override_failure_message(
		"the run must have driven transitions for this law to mean anything",
	).is_greater(0)
	var out_rows := 0
	for i in domains.size():
		if domains[i] == Domain.OUT_OF_DOMAIN:
			out_rows += 1
	assert_int(out_rows).override_failure_message(
		"contact against an equivalently simulated participant must cost nothing",
	).is_equal(0)
	await s.teardown()


# --- what a policy decides, at the engine ---


# The other policies answer "how is a divergence repaired". This one answers
# "can one arise at all", and it answers no by construction rather than by
# repairing well. The peer that does not own the timeline stops simulating, so
# there is no speculative transition to disagree with authority about, and the
# emptiness of the journal is the whole proof.
func test_a_closed_delay_speculates_nothing() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var subject := await s.add_predicted_entity()
	var control := await s.add_predicted_entity()

	subject.client_prediction.configure_recovery({
		policy = RecoveryPolicy.DELAY_CLOSED,
	})
	s.hold_input(subject, RIGHT)
	s.hold_input(control, RIGHT)
	s.run(12)

	assert_int(subject.client_prediction.sim_mode).override_failure_message(
		"a peer that never speculates must not be left in the speculative mode",
	).is_equal(SimMode.DISPLAY)
	# A rig that drove nothing would satisfy an emptiness claim without having
	# simulated anything, so the control entity proves the run had ticks in it.
	assert_int(control.client_prediction.journal().transitions().size()) \
			.override_failure_message(
				"the run must drive an ordinary entity for this law to mean anything",
			).is_greater(0)
	assert_int(subject.client_prediction.journal().transitions().size()) \
			.override_failure_message(
				"a closed delay records no speculative transition, so its divergence "
				+ "is structurally zero rather than merely small",
			).is_equal(0)
	assert_int(subject.client_prediction.corrections).override_failure_message(
		"a transition that never ran cannot be corrected",
	).is_equal(0)
	await s.teardown()


# An observing entity still predicts and still notices, so the divergence has to
# reach the game. What it must not do is move the body: the report IS the whole
# policy, and a hash-guard game that wanted the report would be surprised to find
# its entity rebased underneath it. The control entity is what separates
# "declined to repair" from "never diverged".
func test_an_observing_policy_reports_without_repairing() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var subject := await s.add_predicted_entity()
	var control := await s.add_predicted_entity()
	subject.client_prediction.configure_recovery({
		policy = RecoveryPolicy.OBSERVE,
	})
	var found: Array[int] = []
	var repaired: Array[int] = []
	var control_repaired: Array[int] = []
	subject.client_prediction.divergence_detected.connect(
		func(entry: int, _attribution: int) -> void: found.append(entry),
	)
	subject.client_prediction.recovered.connect(
		func(entry: int, _d: Dictionary, _t: bool, _a: int) -> void:
			repaired.append(entry),
	)
	control.client_prediction.recovered.connect(
		func(entry: int, _d: Dictionary, _t: bool, _a: int) -> void:
			control_repaired.append(entry),
	)

	s.hold_input(subject, RIGHT)
	s.hold_input(control, RIGHT)
	s.run(8)
	s.perturb_server(subject, Vector2(50.0, 0.0))
	s.perturb_server(control, Vector2(50.0, 0.0))
	s.run(12)

	assert_bool(found.is_empty()).override_failure_message(
		"an observing entity must still report the divergence it found, or the "
		+ "policy reports nothing and repairs nothing",
	).is_false()
	# The control proves the same perturbation does reach a repair, so the
	# subject's silence is a policy declining rather than a divergence too small
	# to have been repaired by anyone.
	assert_bool(control_repaired.is_empty()).override_failure_message(
		"the control entity must repair this perturbation, or this rig proves "
		+ "nothing about the one that declined to",
	).is_false()
	assert_array(repaired).override_failure_message(
		"an observing entity reports a divergence and repairs nothing, so it must "
		+ "never announce a recovery it did not perform",
	).is_empty()
	await s.teardown()


# --- bounded convergence, at the engine ---


# A withheld field the sim never re-converges keeps the divergence alive, so
# the recovery train proves non-convergent and the escalation promotes one
# recovery to the full closure: the withheld field is restored without the pose
# error ever crossing the teleport tier. Bounded convergence at every
# configuration is the guarantee, and the one snap is its honest price.
func test_escalation_restores_withheld_fields() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompWithheldBody
	await s.setup(self)
	var subject := await s.add_predicted_entity(
		[&"position", &"velocity", &"boost"],
	)
	var anchor := await s.add_predicted_entity(
		[&"position", &"velocity", &"boost"],
	)
	subject.client_prediction.correction_mode = CorrectionMode.SNAP
	subject.client_prediction.snap_restore = RestoreMode.EXTRAPOLATED
	subject.client_prediction.teleport_threshold = 1000.0
	# Partiality is an in-domain refinement, so the rig declares the island
	# that puts the entity in domain: an undeclared entity would restore the
	# full closure on its first recovery and never reach the escalation.
	subject.client_prediction.configure_island({
		participants = [anchor.client_entity],
	})

	s.hold_input(subject, RIGHT)
	s.hold_input(anchor, RIGHT)
	s.run(12)
	s.reset_metrics(subject)
	var teleport_flags: Array[bool] = []
	subject.client_prediction.recovered.connect(
		func(_e: int, _d: Dictionary, teleported: bool, _a: int) -> void:
			teleport_flags.append(teleported),
	)
	subject.server_root.boost = 5.0
	s.run(40)

	assert_float(subject.client_root.boost).override_failure_message(
		"the escalated recovery must restore the withheld field",
	).is_equal_approx(5.0, 0.01)
	assert_bool(teleport_flags.has(true)).override_failure_message(
		"the closure must arrive as the promoted full restore, reported as a "
		+ "teleport so the display snaps rather than chases",
	).is_true()
	# The train is bounded: the escalation plus the in-flight backlog of
	# already-predicted transitions, never one correction per receive for the
	# whole window.
	assert_int(subject.observer.correction_count).override_failure_message(
		"the recovery train must be bounded by the escalation, not run for the "
		+ "whole window, got %d" % subject.observer.correction_count,
	).is_less(20)
	# And it ends: once the closure lands and the backlog drains, the entity
	# agrees with authority without ever resting its input.
	s.reset_metrics(subject)
	s.run(10)
	assert_int(subject.observer.correction_count).override_failure_message(
		"the divergence must stay closed after the escalated recovery",
	).is_equal(0)
	await s.teardown()


# An entity that never declared its island is out of domain on every
# transition, so each recovery re-bases the whole closure: a field its sim
# never re-converges is restored by the first sub-teleport recovery instead of
# being withheld into a correction train that could only end in a snap.
func test_an_undeclared_entity_recovers_the_full_closure_at_once() -> void:
	var s := PredictionScenario.new()
	s.body_type = LagCompWithheldBody
	await s.setup(self)
	var subject := await s.add_predicted_entity(
		[&"position", &"velocity", &"boost"],
	)
	subject.client_prediction.correction_mode = CorrectionMode.SNAP
	subject.client_prediction.teleport_threshold = 1000.0

	s.hold_input(subject, RIGHT)
	s.run(12)
	var teleport_flags: Array[bool] = []
	subject.client_prediction.recovered.connect(
		func(_e: int, _d: Dictionary, teleported: bool, _a: int) -> void:
			teleport_flags.append(teleported),
	)
	subject.server_root.boost = 5.0
	s.run(20)

	assert_float(subject.client_root.boost).override_failure_message(
		"an out-of-domain recovery must restore the withheld field outright",
	).is_equal_approx(5.0, 0.01)
	assert_bool(teleport_flags.has(true)).override_failure_message(
		"the closure must come from the recovery itself, not a teleport",
	).is_false()
	await s.teardown()
