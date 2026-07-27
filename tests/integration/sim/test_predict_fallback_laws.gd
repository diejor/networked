## Laws for bounded episode fallback and the epoch-aligned return to prediction.
##
## Exhausted recovery evidence closes speculation without closing input. A clean
## authority run earns one reseed, and only a matching command epoch admits the
## evidence-free horizon alignment before episode accounting resumes.
class_name TestPredictFallbackLaws
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const Frames := NetwLagCompensationInterface._PredictFrames
const Handle := NetwLagCompensationInterface.PredictionHandle
const EpisodeState := Handle.EpisodeState
const Operator := NetwPredictJournal.Operator
const Attribution := NetwPredictJournal.Attribution
const RIGHT := { &"motion": Vector2.RIGHT, &"bombing": false }


func _engine() -> NetwLagCompensationInterface._PredictionEngine:
	var engine := Engine_.new()
	engine._handle = Handle.new()
	return engine


func _open(engine: NetwLagCompensationInterface._PredictionEngine) -> void:
	engine._journal.open(0, 0, Handle.DriveKind.FRESH, 1)
	engine._journal.close(0, 1)
	engine._journal.mark_ack(0, false)
	engine._journal.mark_attribution(0, Attribution.PRE_STATE)
	engine._open_episode(0, Attribution.PRE_STATE)
	engine._record_episode_comparison(0, 4, false)


func _bind_position(
		engine: NetwLagCompensationInterface._PredictionEngine,
) -> void:
	var node: Node2D = auto_free(Node2D.new())
	var set := NetwSyncSet.new()
	set.fields.append(
		NetwSyncSet.Field.new(&"position", NetwQuantizeFixed.new()),
	)
	engine._state_binding = NetwSyncSetBinding.new(set, node)


func _authority_witness(
		engine: NetwLagCompensationInterface._PredictionEngine,
		basis: int,
		bits: int = 0,
) -> void:
	engine._authority_witness_classes[basis] = bits


func _empty_ack(epoch: int) -> PackedByteArray:
	return Frames.encode_ack(
		epoch,
		0,
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedInt32Array(),
		PackedByteArray(),
		PackedByteArray(),
	)


func test_structural_budgets_ignore_quality_knobs() -> void:
	var engine := _engine()
	_open(engine)
	engine._episode[&"non_contraction_used"] = \
	Engine_.EPISODE_NON_CONTRACTION_BUDGET - 1
	engine._episode[&"closure_used"] = \
	Engine_.EPISODE_FULL_CLOSURE_BUDGET - 1

	for epsilon in [0.000001, 1.0, 1000000.0]:
		for threshold in [0.000001, 1.0, 1000000.0]:
			engine._handle.divergence_epsilon = epsilon
			engine._handle.teleport_threshold = threshold
			engine._handle.snap_restore = Handle.RestoreMode.EXTRAPOLATED
			assert_bool(engine._episode_budget_exhausted()).is_false()

	engine._episode[&"non_contraction_used"] = \
	Engine_.EPISODE_NON_CONTRACTION_BUDGET
	assert_bool(engine._episode_budget_exhausted()).is_true()
	engine._episode[&"non_contraction_used"] = 0
	engine._episode[&"closure_used"] = \
	Engine_.EPISODE_FULL_CLOSURE_BUDGET
	assert_bool(engine._episode_budget_exhausted()).is_true()


func test_quarantine_needs_distinct_coherent_frames() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 3
	for basis in [1, 2, 3]:
		_authority_witness(engine, basis)

	engine._on_quarantine_state_frame(
		{
			&"tick": 1,
			&"ack": 1,
			&"payload": { &"position": Vector2(1.0, 0.0) },
		},
	)
	engine._on_quarantine_state_frame(
		{
			&"tick": 1,
			&"ack": 1,
			&"payload": { &"position": Vector2(1.0, 0.0) },
		},
	)
	engine._on_quarantine_state_frame(
		{
			&"tick": 2,
			&"ack": 2,
			&"payload": { &"position": Vector2(2.0, 0.0) },
		},
	)

	assert_int(engine._quarantine_clean_run).is_equal(2)
	assert_bool(engine._reseed_align_pending).is_false()
	engine._on_quarantine_state_frame(
		{
			&"tick": 3,
			&"ack": 3,
			&"payload": { &"position": Vector2(3.0, 0.0) },
		},
	)
	assert_bool(engine._fallback_latched).is_false()
	assert_bool(engine._reseed_align_pending).is_true()
	var write: Dictionary = engine.episode()[&"writes"].back()
	assert_int(int(write[&"operator"])).is_equal(Operator.RESEED)
	assert_bool(bool(write[&"evidence_free"])).is_true()


func test_quarantine_waits_for_a_valid_reseed_basis_after_its_run() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 1

	engine._on_quarantine_state_frame({
		&"tick": 1,
		&"ack": -1,
		&"payload": { &"position": Vector2.ONE },
	})
	assert_int(engine._quarantine_clean_run).is_equal(0)
	assert_bool(engine._reseed_align_pending).override_failure_message(
		"a clean row without an authority transition cannot seed the body",
	).is_false()
	_authority_witness(engine, 7)
	engine._on_quarantine_state_frame({
		&"tick": 2,
		&"ack": 7,
		&"payload": { &"position": Vector2(2.0, 0.0) },
	})

	assert_bool(engine._reseed_align_pending).is_true()
	var write: Dictionary = engine.episode()[&"writes"].back()
	assert_int(int(write[&"operator"])).is_equal(Operator.RESEED)
	assert_int(int(write[&"basis"])).is_equal(7)


func test_matching_epoch_admits_one_evidence_free_alignment() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 1
	_authority_witness(engine, 1)
	engine._on_quarantine_state_frame(
		{
			&"tick": 1,
			&"ack": 1,
			&"payload": { &"position": Vector2(1.0, 0.0) },
		},
	)
	engine._role = Handle.Role.PREDICT
	engine._tape_epoch = 9
	engine._correction = Handle.CorrectionMode.SNAP
	engine._latest_input_tick = 4

	engine.receive_ack_frame(_empty_ack(8))
	assert_bool(engine._reseed_epoch_confirmed).is_false()
	engine.receive_ack_frame(_empty_ack(9))
	assert_bool(engine._reseed_epoch_confirmed).is_true()
	var before_non_contraction := int(
		engine.episode()[&"non_contraction_used"],
	)
	var before_closure := int(engine.episode()[&"closure_used"])
	engine._on_state(2, 2, { &"position": Vector2(2.0, 0.0) })

	var report := engine.episode()
	var write: Dictionary = report[&"writes"].back()
	assert_int(int(write[&"operator"])).is_equal(Operator.RESEED_ALIGN)
	assert_bool(bool(write[&"evidence_free"])).is_true()
	assert_int(int(report[&"non_contraction_used"])) \
			.is_equal(before_non_contraction)
	assert_int(int(report[&"closure_used"])).is_equal(before_closure)
	assert_bool(engine._reseed_align_pending).is_false()
	assert_int(int(report[&"state"])).is_equal(EpisodeState.FALLBACK)
	assert_int(engine._reseed_ignore_through).is_equal(4)
	engine._on_state(3, 4, { &"position": Vector2(20.0, 0.0) })
	assert_int(int(engine.episode()[&"state"])) \
			.is_equal(EpisodeState.FALLBACK)


func test_resume_run_scales_with_ack_age_instead_of_wire_width() -> void:
	var engine := _engine()

	assert_int(engine._resume_run_for_ack_age(0)).is_equal(3)
	assert_int(engine._resume_run_for_ack_age(1)).is_equal(3)
	assert_int(engine._resume_run_for_ack_age(3)).is_equal(6)
	assert_int(engine._resume_run_for_ack_age(6)).is_equal(12)
	assert_int(engine._resume_run_for_ack_age(64)).is_equal(128)
	assert_int(engine._resume_run_for_ack_age(1000)).is_equal(256)


func test_fallback_reopens_double_the_ack_derived_quarantine_hold() -> void:
	var handle := Handle.new()
	handle._store_episode_closure(
		{ &"id": 1, &"state": EpisodeState.FALLBACK },
		100,
	)
	var identity := handle._open_episode_identity(101, 12)
	assert_int(int(identity[&"reopened_from"])).is_equal(1)
	assert_int(handle._quarantine_target(12, 256)).is_equal(24)

	handle._store_episode_closure(
		{ &"id": int(identity[&"id"]), &"state": EpisodeState.FALLBACK },
		101,
	)
	handle._open_episode_identity(102, 12)
	assert_int(handle._quarantine_target(12, 256)).is_equal(48)
	handle._store_episode_closure(
		{ &"id": 3, &"state": EpisodeState.FALLBACK },
		102,
	)
	handle._open_episode_identity(103, 12)
	assert_int(handle._quarantine_target(12, 256)).is_equal(96)


func test_quarantine_requires_a_consecutive_clean_authority_witness_run() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 2
	_authority_witness(engine, 1, Handle.WitnessClass.DYNAMIC_ENTITY)
	_authority_witness(engine, 2)
	_authority_witness(engine, 4, Handle.WitnessClass.SUPPORT)
	_authority_witness(engine, 5)

	for basis in [1, 2, 4]:
		engine._on_quarantine_state_frame({
			&"tick": basis,
			&"ack": basis,
			&"payload": { &"position": Vector2(basis, 0.0) },
		})

	assert_int(engine._quarantine_clean_run).override_failure_message(
		"a dirty witness and an authority-basis gap must reset the proof",
	).is_equal(1)
	assert_bool(engine._fallback_latched).is_true()
	engine._on_quarantine_state_frame({
		&"tick": 5,
		&"ack": 5,
		&"payload": { &"position": Vector2(5.0, 0.0) },
	})
	assert_bool(engine._fallback_latched).is_false()
	assert_bool(engine._reseed_align_pending).is_true()


func test_quarantine_reseed_survives_a_stale_state_pool() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 2
	# The state lane delivered one snapshot long ago and its witness bits are
	# already evicted, then the acknowledgement lane proves a clean run far
	# ahead of it.
	engine._on_quarantine_state_frame({
		&"tick": 3,
		&"ack": 3,
		&"payload": { &"position": Vector2(3.0, 0.0) },
	})
	_authority_witness(engine, 40)
	engine._apply_quarantine_witness(40)
	_authority_witness(engine, 41)
	engine._apply_quarantine_witness(41)

	assert_bool(engine._fallback_latched).override_failure_message(
		"a proven run with only a stale snapshot must hold the latch "
		+ "through the bounded wait instead of replaying the past",
	).is_true()
	for basis in range(42, 42 + Engine_.QUARANTINE_RUN_CAP):
		_authority_witness(engine, basis)
		engine._apply_quarantine_witness(basis)

	assert_bool(engine._fallback_latched).override_failure_message(
		"a proven clean run must reseed from the newest retained snapshot "
		+ "instead of holding fallback forever",
	).is_false()
	assert_bool(engine._reseed_align_pending).is_true()
	assert_int(int(engine._episode[&"reseed_transition"])).is_equal(3)


func test_quarantine_reseed_skips_a_known_dirty_snapshot() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 2
	_authority_witness(engine, 2)
	_authority_witness(engine, 3, Handle.WitnessClass.DYNAMIC_ENTITY)
	engine._on_quarantine_state_frame({
		&"tick": 2,
		&"ack": 2,
		&"payload": { &"position": Vector2(2.0, 0.0) },
	})
	engine._on_quarantine_state_frame({
		&"tick": 3,
		&"ack": 3,
		&"payload": { &"position": Vector2(3.0, 0.0) },
	})
	_authority_witness(engine, 40)
	engine._apply_quarantine_witness(40)
	_authority_witness(engine, 41)
	engine._apply_quarantine_witness(41)

	assert_bool(engine._fallback_latched).is_true()
	for basis in range(42, 42 + Engine_.QUARANTINE_RUN_CAP):
		_authority_witness(engine, basis)
		engine._apply_quarantine_witness(basis)

	assert_bool(engine._fallback_latched).is_false()
	assert_int(int(engine._episode[&"reseed_transition"])) \
			.override_failure_message(
		"the newest snapshot carries a dirty witness and must be skipped "
		+ "for the older clean one",
	).is_equal(2)


func test_stale_pool_wait_prefers_a_fresh_seed() -> void:
	var engine := _engine()
	_bind_position(engine)
	_open(engine)
	engine._episode[&"state"] = EpisodeState.FALLBACK
	engine._fallback_latched = true
	engine._quarantine_stream_reconstructed = true
	engine._quarantine_target = 2
	engine._on_quarantine_state_frame({
		&"tick": 1,
		&"ack": 3,
		&"payload": { &"position": Vector2(3.0, 0.0) },
	})
	_authority_witness(engine, 40)
	engine._apply_quarantine_witness(40)
	_authority_witness(engine, 41)
	engine._apply_quarantine_witness(41)

	assert_bool(engine._fallback_latched).override_failure_message(
		"a proven run with only stale snapshots must wait for the lane "
		+ "instead of seeding a pose that predates the divergence",
	).is_true()
	engine._on_quarantine_state_frame({
		&"tick": 2,
		&"ack": 41,
		&"payload": { &"position": Vector2(41.0, 0.0) },
	})

	assert_bool(engine._fallback_latched).is_false()
	assert_bool(engine._reseed_align_pending).is_true()
	assert_int(int(engine._episode[&"reseed_transition"])) \
			.override_failure_message(
		"an in-window row arriving during the wait must seed instead of "
		+ "the stale snapshot",
	).is_equal(41)


func test_epoch_admission_rekeys_the_authority_journal() -> void:
	var engine := _engine()
	engine._command_epoch = 3
	engine._journal.open(0, 100, Handle.DriveKind.FRESH, 111)
	engine._journal.close(0, 7)
	engine._journal.open(1, 101, Handle.DriveKind.FRESH, 222)
	engine._journal.close(1, 8)
	engine._ack = 1

	engine._admit_command_frame({ "epoch": 4 }, [])

	assert_int(engine._journal.size()).override_failure_message(
		"an epoch admission must drop retained rows so a reused index "
		+ "cannot answer with the previous epoch's evidence",
	).is_equal(0)
	assert_int(engine.build_ack_frame().size()).override_failure_message(
		"no acknowledgement may ship until a row of the admitted epoch "
		+ "closes",
	).is_equal(0)
	engine._journal.open(0, 200, Handle.DriveKind.FRESH, 333)
	engine._journal.close(0, 9)
	var row := engine._journal.row_at(0)
	assert_int(int(row[&"c_hash"])).override_failure_message(
		"a reused index must open a fresh row for the admitted epoch",
	).is_equal(333)
	assert_int(int(row[&"label"])).is_equal(200)


func test_state_lane_acks_wait_for_the_epoch_confirmed_ack_lane() -> void:
	var engine := _engine()
	engine._role = Handle.Role.PREDICT
	engine._handle._schedule = Handle.Schedule.FRAME
	engine._tape_epoch = 5
	# A rewire resets the frontier while dead-epoch rows are still in flight.
	engine._ack_domain_confirmed = false
	engine._next_tape_entry_index = 12
	engine._refresh_owner_ack_age()
	assert_int(engine._handle.ack_age_ticks).is_equal(12)

	engine._on_state(500, 400, { })

	assert_int(engine._latest_authority_ack).override_failure_message(
		"an unstamped state-row ack must not feed the frontier before the "
		+ "ack lane confirms this tape's numbering",
	).is_equal(-1)
	assert_int(engine._handle.ack_age_ticks).override_failure_message(
		"a dead-domain ack must not collapse the speculative span",
	).is_equal(12)

	engine.receive_ack_frame(_empty_ack(5))
	engine._on_state(501, 3, { })

	assert_int(engine._latest_authority_ack).override_failure_message(
		"a confirmed domain admits state-row acks again",
	).is_equal(3)
	assert_int(engine._handle.ack_age_ticks).is_equal(8)


func test_adversarial_knobs_still_fallback_with_delayed_control() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	scenario.hold_input(predicted, RIGHT)
	scenario.warmup(predicted, 16)

	var handle := predicted.client_prediction
	handle.divergence_epsilon = 0.000001
	handle.teleport_threshold = 1000000.0
	handle.collision_cooldown_ticks = 0
	handle.snap_restore = Handle.RestoreMode.EXTRAPOLATED
	predicted.client_entity.interpolation.chase_glide_time = 10.0
	# A display channel, so the role the demote resolves is observable. The
	# client owns and controls this body, which is the case that used to
	# resolve DISABLED once the simulation closed.
	var visual := Node2D.new()
	visual.name = "Visual"
	predicted.client_root.add_child(visual)
	predicted.client_entity.interpolation.visual_root = NodePath("Visual")
	Netw.configure_property(predicted.client_root, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	scenario.client.api.interpolation._mark_runtime_dirty(
		predicted.client_entity.interpolation,
	)
	var fallbacks: Array[Dictionary] = []
	var opened: Array[Dictionary] = []
	handle.episode_fallback.connect(
		func(report: Dictionary) -> void: fallbacks.append(report),
	)
	handle.episode_opened.connect(
		func(report: Dictionary) -> void: opened.append(report),
	)
	var corrections_before := predicted.corrections

	var injected_ticks := 0
	while fallbacks.is_empty() and injected_ticks < 48:
		scenario.perturb_server(predicted, Vector2(10.0, 0.0))
		scenario.run(1)
		injected_ticks += 1

	assert_array(fallbacks).override_failure_message(
		"finite structural evidence must stop an adversarial divergence stream",
	).is_not_empty()
	var fallback: Dictionary = fallbacks[0]
	assert_int(int(fallback[&"disposition"][&"state"])) \
			.is_equal(Handle.EpisodeState.FALLBACK)
	var max_verify := 3
	for write: Dictionary in fallback[&"writes"]:
		if not bool(write.get(&"evidence_free", false)):
			max_verify = maxi(max_verify, int(write.get(&"verify_run", 3)))
	var recovery_bound := max_verify \
			+ Engine_.EPISODE_NON_CONTRACTION_BUDGET + 1
	assert_int(predicted.corrections - corrections_before) \
			.override_failure_message(
				"quality knobs may shape repair but cannot expand its evidence budget",
			).is_less_equal(recovery_bound)
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.DISPLAY)

	# A closed simulation stops writing the body, so the display has to consume
	# the replicated stream the way any remote peer does. Resolving DISABLED
	# here freezes the visual on its last value while the collider tracks
	# authority.
	var display_role: int = \
			predicted.client_entity.interpolation.resolved_display_role
	assert_int(display_role).override_failure_message(
		"a demoted body must display from the stream, not stop displaying",
	).is_equal(NetwInterpolationInterface.DisplayRole.REMOTE)

	var consumed_before := predicted.consumed
	var server_position := predicted.server_root.position
	var engine := handle._engine()
	var authored_before := engine._authored_tape.size()
	scenario.run(1)
	assert_int(engine._authored_tape.size()).override_failure_message(
		"fallback must keep authoring the command lane while simulation is closed",
	).is_greater(authored_before)
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.DISPLAY)
	scenario.run(7)
	assert_int(predicted.consumed).override_failure_message(
		"fallback must keep live command delivery instead of zeroing control",
	).is_greater(consumed_before)
	assert_float(predicted.server_root.position.distance_to(server_position)) \
			.is_greater(0.0)

	var opened_at_fallback := opened.size()
	var reentry_ticks := scenario.run_until(
		func() -> bool:
			return not engine._reseed_align_pending \
					and handle.episode().has(&"aligned_transition"),
		Engine_.QUARANTINE_RUN_CAP + 32,
	)
	assert_int(reentry_ticks).override_failure_message(
		"target=%d clean=%d witness=%d states=%s last_basis=%d"
				% [
					engine._quarantine_target,
					engine._quarantine_clean_run,
					engine._authority_witness_classes.size(),
					str(engine._quarantine_pending_states.keys()),
					engine._quarantine_last_basis,
				],
	).is_less(Engine_.QUARANTINE_RUN_CAP + 32)
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.SPECULATIVE)
	assert_int(opened.size()).override_failure_message(
		"the epoch horizon gap belongs to alignment, not a fresh episode",
	).is_equal(opened_at_fallback)
	scenario.run(6)
	assert_int(opened.size()).override_failure_message(
		"post-alignment clean driving must not be priced as fallback flapping",
	).is_equal(opened_at_fallback)
	# The pre-alignment speculation horizon acks with honest mismatches (the
	# client drove from the reseed basis while authority's live body had moved
	# on), so the truthful-acknowledgement law measures after that horizon has
	# flushed: once clean driving is verifying, acknowledgements must carry
	# the admitted epoch's fingerprints, never a previous epoch's retained
	# rows answering for reused indices.
	scenario.run(4)
	var mismatches_settled := handle.fp_mismatch_count
	scenario.run(10)
	assert_int(handle.fp_mismatch_count).override_failure_message(
		"post-alignment acknowledgements must carry the admitted epoch's "
		+ "fingerprints, never a previous epoch's retained rows",
	).is_equal(mismatches_settled)
	await scenario.teardown()
