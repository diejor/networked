## Laws for the domain-aware disturbance episode and its retained evidence.
##
## The episode is behavioral bookkeeping around the existing recovery path. It
## opens only for an actionable settled verdict, pins the causal row outside the
## bounded journal, and retires only after a distinct-transition agreement run.
class_name TestPredictEpisodeLaws
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const Handle := NetwLagCompensationInterface.PredictionHandle
const EpisodeState := Handle.EpisodeState
const Operator := NetwPredictJournal.Operator
const Attribution := NetwPredictJournal.Attribution


func _engine() -> NetwLagCompensationInterface._PredictionEngine:
	var engine := Engine_.new()
	engine._handle = Handle.new()
	return engine


func _append_row(
		engine: NetwLagCompensationInterface._PredictionEngine,
		transition: int,
		attribution: Attribution = Attribution.PRE_STATE,
		matched: bool = false,
		domain: NetwPredictJournal.Domain = NetwPredictJournal.Domain.IN_DOMAIN,
) -> void:
	engine._journal.open(transition, transition, Handle.DriveKind.FRESH, 1)
	engine._journal.close(transition, transition + 1)
	engine._journal.mark_ack(transition, matched)
	engine._journal.mark_attribution(transition, attribution)
	engine._journal.mark_domain(transition, domain)


func _open(engine: NetwLagCompensationInterface._PredictionEngine) -> void:
	_append_row(engine, 0)
	engine._forensic_sidecars[0] = {
		&"pre_bytes": PackedByteArray([1]),
		&"post_bytes": PackedByteArray([2]),
	}
	engine._open_episode(0, Attribution.PRE_STATE)
	engine._record_episode_comparison(0, 4, false)


func _binding(engine: NetwLagCompensationInterface._PredictionEngine) -> void:
	var node: Node2D = auto_free(Node2D.new())
	var set := NetwSyncSet.new()
	set.fields.append(
		NetwSyncSet.Field.new(
			&"position",
			NetwQuantizeFixed.new(),
		),
	)
	engine._state_binding = NetwSyncSetBinding.new(set, node)


func test_meter_zero_region_matches_the_domain_predicates() -> void:
	assert_int(
		Engine_.measure(
			{ &"position": 0.5 },
			{ &"position": 0.5 },
		),
	).is_equal(0)
	assert_int(
		Engine_.measure(
			{ &"position": 1.0 },
			{ &"position": 0.5 },
		),
	).is_equal(1)
	assert_int(
		Engine_.measure(
			{ &"raw": 0.000001 },
			{ &"raw": 0.0 },
		),
	).is_equal(1)


func test_meter_includes_excluded_causal_fields_with_declared_epsilon() -> void:
	var engine := _engine()
	var node: Node2D = auto_free(Node2D.new())
	var set := NetwSyncSet.new()
	set.fields.append(NetwSyncSet.Field.new(&"velocity", null))
	set.fields.append(NetwSyncSet.Field.new(&"unscaled", null))
	engine._state_binding = NetwSyncSetBinding.new(set, node)
	engine._trigger_excludes = { &"velocity": true, &"unscaled": true }
	engine._causal_fields = { &"velocity": true, &"unscaled": true }
	engine._epsilon_overrides = { &"velocity": 0.5 }

	var tolerances := engine._meter_tolerances(
		NetwPredictJournal.Domain.IN_DOMAIN,
	)

	assert_float(float(tolerances[&"velocity"])).is_equal_approx(0.5, 0.0001)
	assert_bool(tolerances.has(&"unscaled")).override_failure_message(
		"reconcile-only causal fields enter the meter only by explicit scale",
	).is_false()


func test_topology_fingerprint_ignores_peer_local_simulation_modes() -> void:
	var engine := _engine()
	var first_root: Node2D = auto_free(Node2D.new())
	var second_root: Node2D = auto_free(Node2D.new())
	var first := NetwEntity.ensure(first_root)
	var second := NetwEntity.ensure(second_root)
	first.entity_id = &"first"
	second.entity_id = &"second"
	engine._handle.island_config = {
		&"epoch": 3,
		&"participants": [second, first],
	}
	first.prediction.sim_mode = Handle.SimMode.DISPLAY
	second.prediction.sim_mode = Handle.SimMode.SPECULATIVE
	var before := engine._base_topology_fingerprint()

	engine._handle.sim_mode = Handle.SimMode.DISPLAY
	first.prediction.sim_mode = Handle.SimMode.AUTHORITATIVE
	second.prediction.sim_mode = Handle.SimMode.DISPLAY

	assert_int(engine._base_topology_fingerprint()).override_failure_message(
		"topology compares membership, not each peer's local realization",
	).is_equal(before)


func test_forensic_mismatch_alone_does_not_open_an_episode() -> void:
	var engine := _engine()
	_append_row(engine, 0, Attribution.CLOSURE)
	assert_dict(engine.episode()).is_empty()


func test_generator_walk_pins_the_first_contiguous_mismatch() -> void:
	var engine := _engine()
	_append_row(engine, 0, Attribution.CLOSURE, true)
	for transition in range(1, 4):
		_append_row(
			engine,
			transition,
			Attribution.PRE_STATE,
			false,
			NetwPredictJournal.Domain.OUT_OF_DOMAIN,
		)
	engine._open_episode(3, Attribution.PRE_STATE)
	var generator: Dictionary = engine.episode()[&"generator_row_copy"]
	assert_int(int(generator[&"transition"])).is_equal(1)


func test_generator_walk_stops_at_an_in_domain_bit_mismatch() -> void:
	var engine := _engine()
	_append_row(engine, 0, Attribution.CLOSURE, true)
	_append_row(engine, 1, Attribution.PRE_STATE)
	for transition in range(2, 4):
		_append_row(
			engine,
			transition,
			Attribution.PRE_STATE,
			false,
			NetwPredictJournal.Domain.OUT_OF_DOMAIN,
		)
	engine._open_episode(3, Attribution.PRE_STATE)
	var generator: Dictionary = engine.episode()[&"generator_row_copy"]
	assert_int(int(generator[&"transition"])).is_equal(2)


func test_one_disturbance_keeps_one_episode_and_attaches_taint() -> void:
	var engine := _engine()
	_open(engine)
	var episode_id := int(engine.episode()[&"id"])
	for transition in range(1, 6):
		_append_row(engine, transition)
		engine._record_episode_divergence(transition)
		engine._record_episode_comparison(transition, 4, false)

	var report := engine.episode()
	assert_int(int(report[&"id"])).is_equal(episode_id)
	assert_int(report[&"taint"].size()).is_equal(5)
	assert_int(report[&"secondary_generators"].size()).is_equal(0)


func test_episode_report_projects_the_stable_diagnostic_sections() -> void:
	var engine := _engine()
	_open(engine)
	engine._episode[&"decisions"].append(
		{
			&"operator": Operator.TRANSPORT_DELTA,
			&"basis": 0,
			&"eligible": false,
			&"applied": false,
			&"eligibility": { &"corridor_clear": false },
		},
	)
	engine._record_episode_write(
		1,
		Operator.REBASE_EXACT,
		0,
		99,
		&"body",
	)

	var report := engine.episode()
	var generator: Dictionary = report[&"generator"]
	assert_int(int(generator[&"transition"])).is_equal(0)
	assert_int(int(generator[&"boundary"])).is_equal(Attribution.PRE_STATE)
	assert_int(int(report[&"operators"].size())).is_equal(2)
	var refusal: Dictionary = report[&"operators"][0]
	assert_bool(refusal[&"eligible"]).is_false()
	assert_bool(refusal[&"applied"]).is_false()
	assert_bool(refusal[&"eligibility"][&"corridor_clear"]).is_false()
	var write: Dictionary = report[&"operators"][1]
	assert_bool(write[&"applied"]).is_true()
	assert_int(int(write[&"outcome"])) \
			.is_equal(Handle.OperatorOutcome.PENDING)
	assert_array(report[&"contraction"]).is_equal(report[&"comparisons"])
	assert_int(int(report[&"disposition"][&"state"])) \
			.is_equal(EpisodeState.OPEN)
	assert_array(report[&"reopen_chain"]).is_equal([1])

	generator[&"row"].clear()
	assert_dict(engine.episode()[&"generator"][&"row"]).is_not_empty()


func test_episode_signals_carry_the_same_stable_report() -> void:
	var engine := _engine()
	var opened: Array[Dictionary] = []
	var closed: Array[Dictionary] = []
	engine._handle.episode_opened.connect(
		func(report: Dictionary) -> void: opened.append(report),
	)
	engine._handle.episode_closed.connect(
		func(report: Dictionary) -> void: closed.append(report),
	)
	_open(engine)
	for transition in range(1, Engine_.EPISODE_CLOSE_RUN + 1):
		engine._record_episode_comparison(transition, 0, true)

	assert_int(opened.size()).is_equal(1)
	assert_dict(opened[0][&"generator"]).is_not_empty()
	assert_int(closed.size()).is_equal(1)
	assert_int(int(closed[0][&"disposition"][&"state"])) \
			.is_equal(EpisodeState.CLOSED)


func test_an_agreeing_pre_failure_pins_a_secondary_generator() -> void:
	var engine := _engine()
	_open(engine)
	_append_row(engine, 1, Attribution.CLOSURE)
	engine._record_episode_divergence(1)

	var report := engine.episode()
	assert_int(report[&"taint"].size()).is_equal(0)
	assert_int(report[&"secondary_generators"].size()).is_equal(1)
	assert_int(int(report[&"secondary_generators"][0][&"transition"])) \
			.is_equal(1)


func test_agreement_requires_the_full_distinct_transition_run() -> void:
	var engine := _engine()
	_open(engine)
	var close_run: int = Engine_.EPISODE_CLOSE_RUN
	for transition in range(1, close_run):
		engine._record_episode_comparison(transition, 0, true)
		engine._record_episode_comparison(transition, 0, true)

	assert_int(int(engine.episode()[&"state"])) \
			.is_equal(EpisodeState.OPEN)
	engine._record_episode_comparison(close_run, 0, true)
	assert_int(int(engine.episode()[&"state"])) \
			.is_equal(EpisodeState.CLOSED)


func test_teleport_write_preserves_episode_evidence() -> void:
	var engine := _engine()
	_binding(engine)
	_open(engine)
	var episode_id := int(engine.episode()[&"id"])
	engine._restore(
		{ &"position": Vector2(4.0, 2.0) },
		Operator.FULL_CLOSURE,
		0,
	)
	engine._correction = Handle.CorrectionMode.SNAP
	engine._track_recovery_convergence(
		false,
		{ &"skip": false, &"teleport": true },
		4.0,
		{ },
		{ },
	)

	var report := engine.episode()
	assert_int(int(report[&"id"])).is_equal(episode_id)
	assert_int(int(report[&"closure_used"])).is_equal(1)
	assert_int(report[&"writes"].size()).is_equal(1)
	assert_int(int(engine._pending_provenance[&"episode"])) \
			.is_equal(episode_id)
	assert_dict(report[&"generator_row_copy"]).is_not_empty()


func test_scope_member_write_uses_the_triggering_episode() -> void:
	var owner := _engine()
	var member := _engine()
	_binding(owner)
	_binding(member)
	_open(owner)
	var episode_id := int(owner.episode()[&"id"])

	member._restore(
		{ &"position": Vector2(3.0, 1.0) },
		Operator.REBASE_EXACT,
		0,
		owner,
	)

	assert_int(int(member._pending_provenance[&"episode"])) \
			.is_equal(episode_id)
	assert_int(owner.episode()[&"writes"].size()).is_equal(1)
	assert_dict(member.episode()).is_empty()


func test_generator_copy_survives_journal_eviction() -> void:
	var engine := _engine()
	_open(engine)
	var pinned: Dictionary = engine.episode()[&"generator_row_copy"]
	for transition in range(1, NetwPredictJournal.CAPACITY_DEFAULT + 20):
		_append_row(engine, transition)

	assert_dict(engine._journal.row_at(0)).is_empty()
	var retained: Dictionary = engine.episode()[&"generator_row_copy"]
	assert_int(int(retained[&"transition"])) \
			.is_equal(int(pinned[&"transition"]))
	assert_that(retained[&"sidecar"][&"pre_bytes"]).is_equal(
		PackedByteArray([1]),
	)


func test_a_nearby_new_episode_links_to_the_closed_one() -> void:
	var engine := _engine()
	_open(engine)
	var close_run: int = Engine_.EPISODE_CLOSE_RUN
	for transition in range(1, close_run + 1):
		engine._record_episode_comparison(transition, 0, true)
	var closed_id := int(engine.episode()[&"id"])

	var reopened_at := close_run + 2
	_append_row(engine, reopened_at)
	engine._open_episode(reopened_at, Attribution.PRE_STATE)
	var report := engine.episode()
	assert_int(int(report[&"reopened_from"])).is_equal(closed_id)
	assert_int(int(report[&"id"])).is_greater(closed_id)
	assert_array(report[&"reopen_chain"]).is_equal(
		[
			closed_id,
			int(report[&"id"]),
		],
	)


func test_absent_witness_is_valid_and_silent() -> void:
	var engine := _engine()
	var evidence := engine._sample_solve_evidence(37, 0)

	assert_int(int(evidence[&"topo_fp"])).is_equal(37)
	assert_int(int(evidence[&"evidence_mask"])).is_equal(0)
	assert_dict(evidence[&"detail"]).is_empty()


func test_malformed_witness_callable_reports_once() -> void:
	var engine := _engine()
	engine._handle.witness().contacts(
		func() -> Dictionary:
			return { colliders = [] },
	)

	await assert_error(
		func() -> void:
			engine._sample_solve_evidence(37, 0)
			engine._sample_solve_evidence(37, 1),
	).is_push_error(
		"Prediction witness must return {colliders: Array, sleeping: bool}; "
		+ "continuous, when present, must be a Dictionary.",
	)
