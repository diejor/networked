## Laws for the experiment-gated present-time transport operator.
##
## The pure kernel composes only pose error onto newer state. Shell eligibility
## requires witnesses the closure reproduces (none, support, static world), an
## entirely clean local horizon, causal non-pose agreement, a sub-teleport
## delta, and a clear body sweep.
class_name TestPredictTransportLaws
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const Handle := NetwLagCompensationInterface.PredictionHandle
const Operator := NetwPredictJournal.Operator
const Attribution := NetwPredictJournal.Attribution


func _engine(corridor_clear: bool = true) -> Engine_:
	var engine := Engine_.new()
	engine._handle = Handle.new()
	engine._handle.recovery_policy = Handle.RecoveryPolicy.REBASE_RECOVER
	engine._handle.teleport_threshold = 2.0
	engine._handle.transport_config = {
		&"corridor": func(_current: Dictionary, _proposed: Dictionary) -> bool:
			return corridor_clear,
	}
	engine._correction = Handle.CorrectionMode.SNAP
	engine._restore_projection = {
		&"position": &"velocity",
		&"heading": &"angular_velocity",
	}
	# What carries a field forward and what the pose error is measured over are
	# two questions, so a rig that reaches past the wiring answers both. They
	# coincide here because a declared derivative channel is the only forward
	# model there is.
	engine._pose_fields = { &"position": true, &"heading": true }
	engine._state_family_of = {
		&"position": Engine_.STATE_FAMILY_POSE,
		&"heading": Engine_.STATE_FAMILY_POSE,
		&"velocity": Engine_.STATE_FAMILY_MOMENTUM,
		&"angular_velocity": Engine_.STATE_FAMILY_MOMENTUM,
		&"latch": Engine_.STATE_FAMILY_CONTROLLER,
	}
	engine._causal_fields = {
		&"position": true,
		&"heading": true,
		&"velocity": true,
		&"angular_velocity": true,
		&"latch": true,
	}
	engine._angle_fields = { &"heading": true }
	return engine


func _append_clean_row(engine: Engine_, transition: int) -> void:
	engine._journal.open(
		transition,
		transition,
		Handle.DriveKind.FRESH,
		1,
	)
	engine._journal.mark_solve(
		transition,
		1,
		2,
		NetwPredictJournal.EVIDENCE_WITNESS,
		{
			&"contact_classes": [Handle.ContactClass.NONE],
			&"sleeping": false,
			&"woke": false,
		},
	)
	engine._journal.close(transition, transition + 1)


func _open_clean_episode(engine: Engine_) -> void:
	_append_clean_row(engine, 4)
	engine._journal.mark_ack(4, false)
	engine._journal.mark_attribution(4, Attribution.PRE_STATE)
	engine._journal.mark_witness_match(4, true)
	_append_clean_row(engine, 5)
	engine._open_episode(4, Attribution.PRE_STATE)
	engine._record_episode_comparison(4, 3, false)


func _predicted() -> Dictionary:
	return {
		&"position": Vector2(2.0, 1.0),
		&"heading": deg_to_rad(179.0),
		&"velocity": Vector2(3.0, 0.0),
		&"angular_velocity": 0.4,
		&"latch": 1.0,
	}


func _authority() -> Dictionary:
	var state := _predicted()
	state[&"position"] = Vector2(2.5, 0.75)
	state[&"heading"] = deg_to_rad(-179.0)
	return state


func _current() -> Dictionary:
	var state := _predicted()
	state[&"position"] = Vector2(12.0, 5.0)
	state[&"heading"] = deg_to_rad(-165.0)
	return state


func test_transport_composes_pose_without_rewinding_newer_progress() -> void:
	var predicted := _predicted()
	var authority := _authority()
	var current := _current()
	var before := current.duplicate(true)
	var result := Engine_.transport(
		predicted,
		authority,
		current,
		{ &"position": true, &"heading": true },
		{ &"heading": true },
	)

	assert_bool(result[&"valid"]).is_true()
	assert_vector(result[&"restore"][&"position"]) \
			.is_equal(Vector2(12.5, 4.75))
	assert_float(
		angle_difference(
			current[&"heading"],
			result[&"restore"][&"heading"],
		),
	).is_equal_approx(deg_to_rad(2.0), 0.00001)
	assert_dict(result[&"restore"]).not_contains_keys(
		[
			&"velocity",
			&"angular_velocity",
			&"latch",
		],
	)
	assert_dict(current).is_equal(before)


func test_clean_agreeing_basis_admits_one_transport_candidate() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	var result := engine._try_transport(
		_predicted(),
		_authority(),
		_current(),
		4,
		false,
	)
	var decision: Dictionary = engine.episode()[&"decisions"][0]

	assert_dict(result).is_not_empty()
	assert_bool(decision[&"eligible"]).is_true()
	assert_bool(decision[&"eligibility"][&"basis_witness_clean"]).is_true()
	assert_bool(decision[&"eligibility"][&"recent_witness_clean"]).is_true()
	assert_bool(decision[&"eligibility"][&"non_pose"][&"agrees"]) \
			.is_true()
	assert_int(int(decision[&"operator"])).is_equal(Operator.TRANSPORT_DELTA)
	assert_dict(
		engine._try_transport(
			_predicted(),
			_authority(),
			_current(),
			4,
			false,
		),
	).is_empty()
	assert_int(engine.episode()[&"decisions"].size()).is_equal(1)


func test_clean_horizon_ignores_only_the_trailing_open_transition() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._journal.open(
		6,
		6,
		Handle.DriveKind.FRESH,
		1,
	)

	assert_bool(engine._recent_witness_clean(4)).is_true()
	assert_bool(engine._recent_witness_clean(6)).is_false()


func test_non_pose_fork_refuses_without_spending_episode_budget() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	var authority := _authority()
	authority[&"velocity"] += Vector2(0.6, 0.0)
	var before_non_contraction := int(
		engine.episode()[&"non_contraction_used"],
	)
	var result := engine._try_transport(
		_predicted(),
		authority,
		_current(),
		4,
		false,
	)
	var decision: Dictionary = engine.episode()[&"decisions"][0]

	assert_dict(result).is_empty()
	assert_bool(decision[&"eligible"]).is_false()
	assert_bool(decision[&"eligibility"][&"non_pose"][&"agrees"]) \
			.is_false()
	assert_int(int(engine.episode()[&"non_contraction_used"])) \
			.is_equal(before_non_contraction)
	assert_int(engine.episode()[&"writes"].size()).is_equal(0)


func test_failed_transport_verification_spends_once_then_descends() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._record_episode_write(
		1,
		Operator.TRANSPORT_DELTA,
		4,
		123,
		&"body",
	)
	for transition in range(5, 8):
		engine._record_episode_comparison(transition, 3, false)
	var report := engine.episode()
	var write: Dictionary = report[&"writes"][0]

	assert_int(int(write[&"outcome"])) \
			.is_equal(Handle.OperatorOutcome.FAILED_TO_CONTRACT)
	assert_int(int(report[&"non_contraction_used"])).is_equal(1)
	assert_bool(engine._transport_pending()).is_false()


func test_clean_momentum_only_error_admits_a_null_dissipate_attempt() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._withheld_fields = {
		&"velocity": true,
		&"angular_velocity": true,
	}
	engine._epsilon_overrides = {
		&"velocity": 0.5,
		&"angular_velocity": 0.35,
	}
	engine._handle.last_field_divergence = {
		&"position": 0.0,
		&"heading": 0.0,
		&"velocity": 0.8,
		&"angular_velocity": 0.6,
		&"latch": 0.0,
	}

	assert_bool(engine._try_dissipate(
		4,
		2,
		NetwPredictJournal.Domain.OUT_OF_DOMAIN,
		false,
	)).is_true()
	var report := engine.episode()
	var decision: Dictionary = report[&"decisions"][0]
	var write: Dictionary = report[&"writes"][0]
	assert_int(int(decision[&"operator"])).is_equal(Operator.DISSIPATE)
	assert_bool(decision[&"eligibility"][&"momentum_active"]).is_true()
	assert_bool(decision[&"eligibility"][&"other_active"]).is_false()
	assert_bool(write[&"null_operator"]).is_true()
	assert_bool(engine._dissipate_pending()).is_true()


func test_dissipate_waits_for_witness_without_a_transport_recipe() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._handle.transport_config = { }
	engine._withheld_fields = { &"velocity": true }

	assert_bool(engine._defer_operator_for_witness(
		6,
		9,
		{ &"velocity": Vector2.ZERO },
	)).is_true()
	assert_int(engine._operator_deferred_basis).is_equal(9)
	assert_bool(engine._deferred_operator_states.has(9)).is_true()


func test_observe_policy_records_but_does_not_run_dissipate() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._handle.recovery_policy = Handle.RecoveryPolicy.OBSERVE
	engine._withheld_fields = { &"velocity": true }
	engine._handle.last_field_divergence = { &"velocity": 0.8 }

	assert_bool(engine._try_dissipate(
		4,
		2,
		NetwPredictJournal.Domain.OUT_OF_DOMAIN,
		false,
	)).is_false()
	var report := engine.episode()
	assert_bool(report[&"decisions"][0][&"eligibility"][&"mechanism"]) \
			.is_false()
	assert_int(report[&"writes"].size()).is_equal(0)


func test_dissipate_waits_through_shrink_and_closes_at_zero() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._record_episode_write(
		1,
		Operator.DISSIPATE,
		4,
		0,
		&"body",
		false,
		true,
	)
	engine._record_episode_comparison(5, 2, false)
	engine._record_episode_comparison(6, 1, false)
	assert_bool(engine._dissipate_pending()).override_failure_message(
		"each strict shrink must extend the no-write observation",
	).is_true()
	engine._record_episode_comparison(7, 0, true)

	var report := engine.episode()
	var write: Dictionary = report[&"writes"][0]
	assert_int(int(write[&"outcome"])) \
			.is_equal(Handle.OperatorOutcome.CONTRACTED)
	assert_int(int(report[&"non_contraction_used"])).is_equal(0)
	assert_bool(engine._dissipate_pending()).is_false()


func test_stalled_dissipate_spends_once_then_descends() -> void:
	var engine := _engine()
	_open_clean_episode(engine)
	engine._record_episode_write(
		1,
		Operator.DISSIPATE,
		4,
		0,
		&"body",
		false,
		true,
	)
	for transition in range(5, 5 + Engine_.EPISODE_CLOSE_RUN):
		engine._record_episode_comparison(transition, 3, false)

	var report := engine.episode()
	var write: Dictionary = report[&"writes"][0]
	assert_int(int(write[&"outcome"])) \
			.is_equal(Handle.OperatorOutcome.FAILED_TO_CONTRACT)
	assert_int(int(report[&"non_contraction_used"])).is_equal(1)
	assert_bool(engine._dissipate_pending()).is_false()


func test_dirty_horizon_and_blocked_corridor_each_refuse() -> void:
	var dirty := _engine()
	_open_clean_episode(dirty)
	dirty._journal._witness_details[5][&"contact_classes"] = [
		Handle.ContactClass.KINEMATIC_PROXY,
	]
	assert_dict(
		dirty._try_transport(
			_predicted(),
			_authority(),
			_current(),
			4,
			false,
		),
	).is_empty()
	assert_bool(
		dirty.episode()[&"decisions"][0][&"eligibility"] \
		[&"recent_witness_clean"],
	).is_false()

	var blocked := _engine(false)
	_open_clean_episode(blocked)
	assert_dict(
		blocked._try_transport(
			_predicted(),
			_authority(),
			_current(),
			4,
			false,
		),
	).is_empty()
	assert_bool(
		blocked.episode()[&"decisions"][0][&"eligibility"] \
		[&"corridor_clear"],
	).is_false()


func test_real_lane_uses_transport_once_while_input_keeps_advancing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	scenario.body_type = LagCompForecastBody
	var predicted := await scenario.add_predicted_entity(
		[
			&"position",
			&"velocity",
		],
	)
	var witness := func() -> Dictionary:
		return { &"colliders": [], &"sleeping": false }
	predicted.client_prediction.witness().contacts(witness)
	predicted.server_prediction.witness().contacts(witness)
	predicted.client_prediction.transport().corridor(
		func(
				_current_state: Dictionary,
				_proposed_state: Dictionary,
		) -> bool:
			return true,
	)
	predicted.client_prediction.recovery() \
			.policy(Handle.RecoveryPolicy.REBASE_RECOVER) \
			.projection(Handle.RestoreMode.EXTRAPOLATED) \
			.teleport_threshold(4.0)
	scenario.hold_input(
		predicted,
		{ &"motion": Vector2.RIGHT, &"bombing": false },
	)
	scenario.warmup(predicted, Engine_.EPISODE_CLOSE_RUN + 24)
	var before := predicted.client_root.position
	scenario.perturb_server(predicted, Vector2(0.5, 0.0))
	var ran := scenario.run_until(
		func() -> bool:
			var report := predicted.client_prediction.episode()
			for write: Dictionary in report.get(&"writes", []):
				if int(write.get(&"operator", -1)) == Operator.TRANSPORT_DELTA:
					return true
			return false,
		24,
	)
	var report := predicted.client_prediction.episode()
	var transports := 0
	for write: Dictionary in report.get(&"writes", []):
		if int(write.get(&"operator", -1)) == Operator.TRANSPORT_DELTA:
			transports += 1

	assert_int(ran).is_less(24)
	assert_int(transports).is_equal(1)
	assert_bool(report[&"decisions"][0][&"eligible"]).is_true()
	assert_float(predicted.client_root.position.x) \
			.override_failure_message(
				"new local commands must advance while transport reconciles",
			).is_greater(before.x)
	var closed_after := scenario.run_until(
		func() -> bool:
			return int(predicted.client_prediction.episode().get(&"state", -1)) \
					== Handle.EpisodeState.CLOSED,
		Engine_.EPISODE_CLOSE_RUN + 16,
	)
	report = predicted.client_prediction.episode()
	var transport: Dictionary = report[&"writes"][0]
	assert_int(int(transport[&"outcome"])) \
			.is_equal(Handle.OperatorOutcome.CONTRACTED)
	assert_int(closed_after).is_less(Engine_.EPISODE_CLOSE_RUN + 16)
	assert_int(int(report[&"state"])).is_equal(Handle.EpisodeState.CLOSED)
	await scenario.teardown()
