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
	var before := engine._base_topology_fingerprint(1)

	engine._handle.sim_mode = Handle.SimMode.DISPLAY
	first.prediction.sim_mode = Handle.SimMode.AUTHORITATIVE
	second.prediction.sim_mode = Handle.SimMode.DISPLAY

	assert_int(engine._base_topology_fingerprint(1)).override_failure_message(
		"topology compares membership, not each peer's local realization",
	).is_equal(before)


# The amount of simulated time a transition bought is an execution fact about
# that transition, so two peers that spent different amounts of physics on the
# same transition disagree at the topology boundary rather than exhausting the
# ladder and reporting that every antecedent agreed.
func test_topology_fingerprint_separates_transitions_by_their_quantum() -> void:
	var engine := _engine()

	assert_int(engine._base_topology_fingerprint(2)).override_failure_message(
		"a transition that spent two physics steps must not fingerprint as one",
	).is_not_equal(engine._base_topology_fingerprint(1))


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
	for transition in range(1, engine._episode_close_run() + 1):
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
	var close_run: int = engine._episode_close_run()
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
	var close_run: int = engine._episode_close_run()
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


# An episode has no bound on how long it may stay open, so its evidence must not
# grow with session length. The trim keeps the newest entries and reports how
# many it elided, which is what separates a truncated series from a short one.
func test_evidence_series_stay_bounded_and_count_what_they_drop() -> void:
	var engine := _engine()
	_open(engine)
	var limit: int = Engine_.EPISODE_EVIDENCE_LIMIT
	var extra := 50
	for transition in range(1, limit + extra):
		engine._record_episode_comparison(transition, 4, false)

	# The opening comparison plus the loop is limit + extra entries seen.
	var report := engine.episode()
	assert_int(report[&"comparisons"].size()).is_equal(limit)
	assert_int(int(report[&"disposition"][&"evidence_dropped"])).is_equal(extra)
	# The newest survive, so contraction stays judgeable at the frontier.
	assert_int(int(report[&"comparisons"].back()[&"transition"])) \
			.is_equal(limit + extra - 1)
	# The generator predates every trimmed entry and is pinned outside them.
	assert_int(int(report[&"generator"][&"transition"])).is_equal(0)
	assert_dict(report[&"generator"][&"row"]).is_not_empty()


# Independent failures are pinned row copies and the earliest are the causally
# interesting ones, so the cap refuses new pins rather than dropping old ones.
func test_secondary_generators_cap_and_keep_the_earliest() -> void:
	var engine := _engine()
	_open(engine)
	var cap: int = Engine_.EPISODE_GENERATOR_LIMIT
	for transition in range(1, cap + 5):
		_append_row(engine, transition, Attribution.CLOSURE)
		engine._record_episode_divergence(transition)

	var report := engine.episode()
	assert_int(report[&"secondary_generators"].size()).is_equal(cap)
	assert_int(int(report[&"secondary_generators"][0][&"transition"])) \
			.is_equal(1)
	assert_int(int(report[&"disposition"][&"evidence_dropped"])).is_equal(4)


# One projection serves the detached report and the per-frame digest, so a
# reader that takes the cheap path can never see a different disposition.
func test_the_digest_reports_the_disposition_without_the_evidence() -> void:
	var engine := _engine()
	_open(engine)
	engine._record_episode_write(1, Operator.REBASE_EXACT, 0, 99, &"body")
	for transition in range(1, 9):
		engine._record_episode_comparison(transition, 3, false)

	var report := engine.episode()
	var digest := engine._handle.episode_digest()
	assert_dict(digest[&"disposition"]).is_equal(report[&"disposition"])
	assert_int(int(digest[&"id"])).is_equal(int(report[&"id"]))
	assert_int(int(digest[&"generator"][&"transition"])) \
			.is_equal(int(report[&"generator"][&"transition"]))
	assert_int(int(digest[&"last_meter"])).is_equal(3)
	assert_int(int(digest[&"evidence"][&"comparisons"])) \
			.is_equal(report[&"comparisons"].size())
	assert_int(int(digest[&"last_operator"][&"operator"])) \
			.is_equal(Operator.REBASE_EXACT)
	# It carries no series at all, which is the whole point of it.
	assert_bool(digest.has(&"comparisons")).is_false()
	assert_bool(digest.has(&"contraction")).is_false()


# The revision is what lets a recorder detect a change without detaching or
# serializing the record.
func test_the_digest_revision_moves_only_on_evidence_mutation() -> void:
	var engine := _engine()
	_open(engine)
	var first := int(engine._handle.episode_digest()[&"revision"])
	assert_int(int(engine._handle.episode_digest()[&"revision"])) \
			.is_equal(first)
	engine._record_episode_comparison(1, 2, false)
	assert_int(int(engine._handle.episode_digest()[&"revision"])) \
			.is_greater(first)


# The handle adopts the engine's record instead of copying it on every mutation,
# so the public read is the one place a copy still has to happen.
func test_a_detached_report_survives_later_evidence() -> void:
	var engine := _engine()
	_open(engine)
	var report := engine.episode()
	var before: int = report[&"comparisons"].size()
	engine._record_episode_comparison(1, 2, false)

	assert_int(report[&"comparisons"].size()).is_equal(before)
	assert_int(engine.episode()[&"comparisons"].size()).is_equal(before + 1)


# A transition is a fixed quantum of simulated time. The physics server runs one
# step per frame while the drive runs on its own cadence, so the count of steps
# between two consecutive drives is the local evidence that the two agree, and
# it is the one divergence cause a peer can detect without a peer.
func test_the_quantum_counts_physics_steps_between_drives() -> void:
	var engine := _engine()
	engine._declared_quantum_value = 1

	# A first drive has nothing to measure against and reports the declaration,
	# because a peer-local start must never read as a cross-peer mismatch.
	engine._frame_index = 10
	assert_int(engine._measure_quantum()).is_equal(1)

	# One drive per physics frame is the declared quantum.
	engine._frame_index = 11
	assert_int(engine._measure_quantum()).is_equal(1)

	# A frame that ran no drive is charged to the transition that follows it.
	engine._frame_index = 13
	assert_int(engine._measure_quantum()).is_equal(2)

	# Two drives inside one frame share one integration, the same fault seen
	# from the other side.
	assert_int(engine._measure_quantum()).is_equal(0)


# A rewire is peer-local, so the gap it opens must not be charged to the first
# transition after it.
func test_a_rewire_re_anchors_the_quantum_on_the_declaration() -> void:
	var engine := _engine()
	engine._declared_quantum_value = 2
	engine._frame_index = 100
	engine._measure_quantum()
	engine._last_drive_frame = -1
	engine._frame_index = 400

	assert_int(engine._measure_quantum()).is_equal(2)


# A stall must not hand the topology fingerprint a fresh value for every frame
# it lost, since one saturated count already says the transition is unusable.
func test_the_quantum_saturates_rather_than_counting_a_stall() -> void:
	var engine := _engine()
	engine._frame_index = 1
	engine._measure_quantum()
	engine._frame_index = 5000

	assert_int(engine._measure_quantum()) \
			.is_equal(Engine_.QUANTUM_STEPS_MAX)


# An engine driven with no interface pump has no frames to count, so it reports
# the declaration rather than manufacturing a fault out of its own absence.
func test_an_unpumped_engine_reports_the_declared_quantum() -> void:
	var engine := _engine()
	engine._declared_quantum_value = 3

	assert_int(engine._measure_quantum()).is_equal(3)
	assert_int(engine._measure_quantum()).is_equal(3)


# The closure run scales with the horizon this session carries, because that is
# what a closure has to survive. Pinned to the wire redundancy constant instead,
# a run of 64 exact agreements needs (1-p)^64, which is a coin flip at a one
# percent disagreement rate and unreachable at five.
func test_the_closure_run_scales_with_the_acknowledgement_age() -> void:
	var engine := _engine()

	engine._handle.ack_age_ticks = 0
	assert_int(engine._episode_close_run()).override_failure_message(
		"a run still has to be a run, however short the horizon",
	).is_equal(Engine_.EPISODE_CLOSE_RUN_MIN)

	engine._handle.ack_age_ticks = 12
	assert_int(engine._episode_close_run()).is_equal(24)

	engine._handle.ack_age_ticks = 4096
	assert_int(engine._episode_close_run()).override_failure_message(
		"the run must stay inside the evidence the wire actually retains",
	).is_equal(Engine_.EPISODE_CLOSE_RUN)


# Doubling a quarantine hold is sound against a transient of unknown duration
# and pure cost against a standing one, because the wait buys nothing and the
# window it opens is where an unsupervised prediction drifts far enough that its
# repair reads as a teleport.
func test_a_standing_boundary_does_not_lengthen_the_next_hold() -> void:
	var handle := Handle.new()
	var base := Engine_.RESUME_RUN_MIN
	var cap := Engine_.QUARANTINE_RUN_CAP

	# A fallback, then a reopen against a different boundary: transient, priced.
	handle._open_episode_identity(0, Engine_.EPISODE_FLAP_WINDOW, Attribution.CONTACT)
	handle._store_episode_closure(
		{ &"id": 1, &"state": EpisodeState.FALLBACK },
		0,
	)
	handle._open_episode_identity(1, Engine_.EPISODE_FLAP_WINDOW, Attribution.COMMAND)
	var transient := handle._quarantine_target(base, cap)

	# The same fallback, reopened against the boundary that already failed.
	handle._store_episode_closure(
		{ &"id": 2, &"state": EpisodeState.FALLBACK },
		1,
	)
	handle._open_episode_identity(2, Engine_.EPISODE_FLAP_WINDOW, Attribution.COMMAND)
	var standing := handle._quarantine_target(base, cap)

	assert_int(transient).override_failure_message(
		"a transient reopen must still be priced, or the guard is inert",
	).is_greater(base)
	assert_int(standing).override_failure_message(
		"a reopen against the boundary that already failed must not buy a "
		+ "longer wait, because waiting is not what fixes it",
	).is_equal(transient)


# A transition that spent the wrong amount of physics is standing by
# construction: no length of wait changes how much simulated time a frame buys.
func test_a_topology_boundary_never_lengthens_a_hold() -> void:
	var handle := Handle.new()
	var base := Engine_.RESUME_RUN_MIN
	var cap := Engine_.QUARANTINE_RUN_CAP

	handle._open_episode_identity(0, Engine_.EPISODE_FLAP_WINDOW, Attribution.CONTACT)
	handle._store_episode_closure(
		{ &"id": 1, &"state": EpisodeState.FALLBACK },
		0,
	)
	handle._open_episode_identity(1, Engine_.EPISODE_FLAP_WINDOW, Attribution.TOPOLOGY)

	assert_int(handle._quarantine_target(base, cap)).override_failure_message(
		"a quantum mismatch is standing, so a longer quarantine only grows the "
		+ "unsupervised window",
	).is_equal(base)
