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


# A pass that ran no transition claims none, whether it declined one it held or
# had none to decline. The journal records what ran, so a frame absent from it
# is a frame that drove nothing.
#
# This is also the cost the FRAME tier's zero-depth default exists to avoid, and
# the reason it cannot avoid all of it. The space solves on both of these
# frames, so each is simulated time no transition accounts for and each shows up
# on the next drive as a [member quantum_fault_count]. Authority cannot hold the
# space instead: the gate holds a whole world and the quantum is per entity, so
# one dry car cannot stop the others. The remaining answer is for authority to
# run a substituted transition and declare it, which it cannot do while the
# transition index belongs to the owner alone — the owner authors the index this
# cursor walks, so authority has no identity to file its own transition under.
func test_frame_a_pass_that_drove_nothing_claims_no_transition() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	_warm_replay(scenario, predicted)

	var journal := predicted.server_prediction.journal()
	var rows_before := journal.size()
	var held_before := predicted.server_prediction.held_count

	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.held_count).is_equal(held_before + 1)
	assert_int(journal.size()).override_failure_message(
		"a frame that drove nothing must not claim a transition",
	).is_equal(rows_before)
	assert_int(predicted.server_state.reconcile_ack) \
			.override_failure_message("a held pass acknowledges nothing") \
			.is_equal(-1)

	# One transition still stands behind the buffer. Spend it, so the frame after
	# it is dry rather than merely buffered.
	predicted.server_prediction.replay_buffer_depth = 0
	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_prediction.tape_queue_depth).override_failure_message(
		"the queue must actually be empty, or the next pass is a hold and this "
		+ "half of the law measures nothing",
	).is_equal(0)

	var rows_dry := journal.size()
	predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.starved_count).is_equal(1)
	assert_int(journal.size()).override_failure_message(
		"a dry frame drove nothing, so it claims nothing either",
	).is_equal(rows_dry)


# Queue depth is what covers a dry frame, not the buffer setting. An arrival
# that lands early leaves a transition standing, and the frame that receives
# nothing runs that one instead of holding. The buffer only decides which depth
# the same pattern plays out around, so this reads identically at any setting.
func test_frame_depth_from_an_early_arrival_covers_a_late_one() -> void:
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


# A dry run acknowledges nothing and leaves the cursor exactly where the owner's
# next transition will land.
#
# The second half is the constraint any future substitution has to satisfy and
# the reason one cannot simply be inserted here. The index this cursor walks is
# authored by the owner, so a transition authority invents would have to occupy
# an index the owner is still going to fill. Advancing the cursor past it stops
# the owner's stream being consumed at all, which is a worse failure than the
# quantum fault a dry frame costs.
func test_frame_a_dry_run_acks_nothing_and_strands_no_transition() -> void:
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

	var dry := 4
	for index in dry:
		predicted.server_prediction.simulate_frame(scenario.dt())

	assert_int(predicted.server_prediction.starved_count).is_equal(dry)
	assert_int(predicted.server_state.reconcile_ack) \
			.override_failure_message("a dry pass acknowledges nothing") \
			.is_equal(-1)

	# The owner's next transition is consumed on the frame after it arrives. A
	# dry run must cost the stream nothing but the frames it had no input for.
	var consumed_before := predicted.server_prediction.consumed_count
	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	predicted.server_prediction.simulate_frame(scenario.dt())
	assert_int(predicted.server_prediction.consumed_count) \
			.override_failure_message(
				"a dry run must not strand the cursor ahead of the stream, or "
				+ "the owner's transitions stop being consumed",
			).is_equal(consumed_before + 1)


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


# A physics frame that advanced the world but opened no transition is the one
# divergence cause a peer can name alone: the solver moved by an amount no
# compared column accounts for, so every antecedent still agrees and the
# boundary ladder would otherwise walk all the way to CLOSURE.
#
# The pump here emits one frame per call, so the clock is set to declare one
# physics step per transition and the harness matches what it claims.
func test_a_frame_that_opens_no_transition_is_charged_to_the_next_one() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	_quantize_motion(predicted)
	scenario.client_clock.tickrate = Engine.physics_ticks_per_second
	predicted.client_root.motion = Vector2.RIGHT

	for _index in 3:
		_emit_client_frame(scenario.client_clock, 1)
	var handle := predicted.client_prediction
	assert_int(handle.quantum_declared).override_failure_message(
		"the harness must declare the cadence it actually pumps",
	).is_equal(1)
	assert_int(handle.quantum_steps).override_failure_message(
		"one drive per physics frame is the declared quantum",
	).is_equal(1)
	var clean := handle.quantum_fault_count

	# The clamped frame authors nothing, so the drive after it stands alone for
	# two frames of physics.
	_emit_client_frame(scenario.client_clock, 0)
	_emit_client_frame(scenario.client_clock, 1)

	assert_int(handle.quantum_steps).is_equal(2)
	assert_int(handle.quantum_fault_count).override_failure_message(
		"a frame that integrates without a transition must be counted",
	).is_equal(clean + 1)
	assert_int(int(handle.stats()[&"quantum_faults"])).is_equal(clean + 1)
