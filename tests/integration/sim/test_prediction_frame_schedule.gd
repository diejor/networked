class_name TestPredictionFrameSchedule
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME


class IdleSensitiveBody extends LagCompSimBody:
	var simulation_steps: int = 0


	func _network_tick(
			delta: float,
			tick: int,
			is_fresh: bool,
	) -> void:
		simulation_steps += 1
		super._network_tick(delta, tick, is_fresh)


func test_frame_tape_replays_the_same_simulation_entry_for_entry() -> void:
	var scenario := PredictionScenario.new()
	scenario.body_type = IdleSensitiveBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	_quantize_motion(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 0, 2, 1, 1, 0, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		predicted.server_prediction.simulate_frame(scenario.dt())
		var ack := predicted.server_state.reconcile_ack
		if ack < 0:
			continue
		var expected := predicted.client_prediction.transition_state_at(ack)
		assert_dict(expected).is_not_empty()
		assert_int(predicted.server_root.simulation_steps).is_equal(ack + 1)
		assert_that(predicted.server_root.position) \
				.override_failure_message(
					"author and replay states must be bit-equal at each entry ack",
				).is_equal(expected[&"position"])

	assert_array(predicted.server_prediction.tape_transitions()).is_equal(
		predicted.client_prediction.tape_transitions(),
	)
	assert_int(predicted.server_prediction.folded_count).is_equal(0)
	assert_int(predicted.server_prediction.held_count).is_equal(3)
	assert_int(predicted.server_prediction.starved_count).is_equal(0)
	assert_int(predicted.client_prediction.authoring_clamped_count).is_equal(2)


func test_frame_authors_at_most_one_transition_per_network_tick() -> void:
	var scenario := PredictionScenario.new()
	scenario.body_type = IdleSensitiveBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	_emit_client_frame(scenario.client_clock, 1)
	var transitions: int = predicted.client_prediction.tape_transitions().size()
	var steps: int = predicted.client_root.simulation_steps
	_emit_client_frame(scenario.client_clock, 0)
	_emit_client_frame(scenario.client_clock, 0)

	assert_int(predicted.client_prediction.tape_transitions().size()) \
			.is_equal(transitions)
	assert_int(predicted.client_root.simulation_steps).is_equal(steps)
	assert_int(predicted.client_prediction.authoring_clamped_count).is_equal(2)


func test_prediction_holds_at_the_structural_ack_age_ceiling() -> void:
	var scenario := PredictionScenario.new()
	scenario.body_type = IdleSensitiveBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	var ceiling := \
			NetwLagCompensationInterface._PredictionEngine.ACK_AGE_MAX
	for index in ceiling:
		_emit_client_frame(scenario.client_clock, 1)
	var transitions: int = predicted.client_prediction.tape_transitions().size()
	var steps: int = predicted.client_root.simulation_steps

	_emit_client_frame(scenario.client_clock, 1)

	assert_int(predicted.client_prediction.tape_transitions().size()) \
			.is_equal(transitions)
	assert_int(predicted.client_root.simulation_steps).is_equal(steps)
	assert_int(predicted.client_prediction.speculation_held_count).is_equal(1)
	assert_int(predicted.client_prediction.stats()[&"ack_age_max"]) \
			.is_equal(ceiling)


func test_frame_journal_fingerprints_match_at_every_acked_entry() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	_quantize_motion(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 0, 2, 1, 1, 0, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		scenario.server_sim.before_frame_step()
		predicted.server_prediction.simulate_frame(scenario.dt())
		_deliver_ack(predicted)

	var journal := predicted.client_prediction.journal()
	var transitions := journal.transitions()
	var flags := journal.flags()
	var verified: Array[int] = []
	for i in transitions.size():
		if flags[i] & NetwPredictJournal.ROW_ACKED:
			verified.append(transitions[i])
	assert_array(verified).override_failure_message(
		"the law is vacuous unless entries were actually acked and closed",
	).is_not_empty()

	for i in transitions.size():
		if not (flags[i] & NetwPredictJournal.ROW_ACKED):
			continue
		assert_bool(flags[i] & NetwPredictJournal.ROW_MATCHED > 0) \
				.override_failure_message(
					"transition %d ran the same closure on both peers, so its "
					% transitions[i] + "fingerprints must be equal",
				).is_true()

	assert_int(predicted.client_prediction.fp_mismatch_count).is_equal(0)
	assert_int(predicted.client_prediction.first_divergent_transition) \
			.is_equal(-1)
	assert_int(journal.first_unmatched()).is_equal(verified.back() + 1)


func test_frame_journal_command_hashes_agree_across_the_peers() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	_quantize_motion(predicted)

	var steered := [Vector2.RIGHT, Vector2.UP, Vector2.LEFT, Vector2.DOWN]
	for index in 8:
		predicted.client_root.motion = steered[index % steered.size()]
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
		predicted.server_prediction.simulate_frame(scenario.dt())

	var client := predicted.client_prediction.journal()
	var server := predicted.server_prediction.journal()
	var replayed := server.transitions()
	assert_array(replayed).is_not_empty()
	for transition: int in replayed:
		var server_row := server.row_at(transition)
		var client_row := client.row_at(transition)
		assert_dict(client_row).is_not_empty()
		assert_int(server_row[&"label"]).is_equal(client_row[&"label"])
		assert_int(server_row[&"c_hash"]).override_failure_message(
			"authority ran a different command than the owner authored at "
			+ "transition %d" % transition,
		).is_equal(client_row[&"c_hash"])


func test_frame_held_and_starved_frames_append_no_journal_row() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	_warm_replay(scenario, predicted)

	var server := predicted.server_prediction.journal()
	var before := server.size()
	var held_before := predicted.server_prediction.held_count

	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.held_count).is_equal(held_before + 1)
	assert_int(server.size()).override_failure_message(
		"a frame that drove nothing must not claim a transition",
	).is_equal(before)


func test_frame_buffer_absorbs_one_early_then_late_arrival() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_prediction.held_count).is_equal(1)

	for index in 2:
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())
	var held_after_warmup := predicted.server_prediction.held_count
	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.held_count).is_equal(
		held_after_warmup,
	)
	assert_int(predicted.server_prediction.tape_queue_depth).is_equal(1)


func test_frame_one_missed_arrival_causes_exactly_one_hold() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	_warm_replay(scenario, predicted)
	var held_before := predicted.server_prediction.held_count
	var ack_before := predicted.server_state.reconcile_ack

	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_prediction.held_count).is_equal(
		held_before + 1,
	)
	assert_int(predicted.server_state.reconcile_ack) \
			.override_failure_message("a held pass acknowledges nothing") \
			.is_equal(-1)

	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_prediction.held_count).is_equal(
		held_before + 1,
	)
	assert_int(predicted.server_state.reconcile_ack).is_equal(ack_before + 1)


func test_frame_dry_queue_acks_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.server_prediction.replay_buffer_depth = 0
	predicted.client_root.motion = Vector2.RIGHT

	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_state.reconcile_ack).is_greater_equal(0)
	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.starved_count).is_equal(1)
	assert_int(predicted.server_state.reconcile_ack) \
			.override_failure_message("a dry pass acknowledges nothing") \
			.is_equal(-1)


func test_frame_resync_jumps_to_live_edge_and_client_corrects() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.server_prediction.max_consume_lag_ticks = 2
	predicted.client_root.motion = Vector2.RIGHT

	for index in 6:
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.resync_count).is_equal(1)
	assert_int(predicted.server_prediction.skipped_count).is_equal(4)
	assert_int(predicted.server_state.reconcile_ack).is_equal(4)
	assert_int(predicted.server_prediction.tape_queue_depth).is_equal(1)
	_deliver_state(predicted, 10)
	assert_int(predicted.client_prediction.corrections).is_equal(1)
	assert_int(predicted.client_prediction.last_compare_staleness).is_equal(0)


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.server_prediction.missing_policy = \
	NetwLagCompensationInterface.PredictionHandle.MissingInput.REPEAT_LAST


func _quantize_motion(predicted: PredictedEntity) -> void:
	for binding: NetwSyncSetBinding in [
		predicted.client_input,
		predicted.server_input,
	]:
		for field in binding.set.fields:
			if field.key != &"motion":
				continue
			field.quantizer = NetwQuantizeBits.new().bits(16).limits(-1.0, 1.0)


func _warm_replay(
		scenario: PredictionScenario,
		predicted: PredictedEntity,
) -> void:
	for index in 2:
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
		predicted.server_prediction.simulate_frame(scenario.dt())


func _deliver_command(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


func _deliver_ack(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.server_prediction._engine().build_ack_frame()
	if not bytes.is_empty():
		predicted.client_prediction._engine().receive_ack_frame(bytes)


func _deliver_state(predicted: PredictedEntity, tick: int) -> void:
	var ack := predicted.server_state.reconcile_ack
	var bytes := predicted.server_state.encode_volatile(0, tick, ack)
	predicted.client_state.apply_volatile(bytes)


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()
