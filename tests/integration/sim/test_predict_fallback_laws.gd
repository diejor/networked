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
	await scenario.teardown()
