## Laws for the [constant Schedule.FRAME] tier riding the prediction lanes.
##
## Authority replays from the owner lane's queue, where a transition arrives
## with the command that drove it, so there is no second stream to look a
## command up in. The laws that matter are that replay stays exact, and that
## the one substitution the protocol admits is declared rather than silent.
class_name TestPredictLanesFrame
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME
const PredictFrames := NetwLagCompensationInterface._PredictFrames


class StepCountingBody extends LagCompSimBody:
	var simulation_steps: int = 0


	func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
		simulation_steps += 1
		super._network_tick(delta, tick, is_fresh)


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule = FRAME
	predicted.server_prediction.schedule = FRAME
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.server_prediction.missing_policy = \
	NetwLagCompensationInterface.PredictionHandle.MissingInput.REPEAT_LAST


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()


func _deliver_command(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


# Authority records the state its previous frame produced before driving the
# next one, which is the ordering that closes a journal row. A rig that drove
# the replay alone would leave every row open and acknowledge a fingerprint
# authority had not computed.
func _step_authority(
		scenario: PredictionScenario,
		predicted: PredictedEntity,
) -> void:
	scenario.server_sim.before_frame_step()
	predicted.server_prediction.simulate_frame(scenario.dt())


func _deliver_ack(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.server_prediction._engine().build_ack_frame()
	if not bytes.is_empty():
		predicted.client_prediction._engine().receive_ack_frame(bytes)


func test_authority_replays_the_owner_lane_entry_for_entry() -> void:
	var scenario := PredictionScenario.new()
	scenario.body_type = StepCountingBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 0, 2, 1, 1, 0, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)
		var ack := predicted.server_state.reconcile_ack
		if ack < 0:
			continue
		var expected := predicted.client_prediction.transition_state_at(ack)
		assert_dict(expected).is_not_empty()
		assert_int(predicted.server_root.simulation_steps).is_equal(ack + 1)
		assert_that(predicted.server_root.position).override_failure_message(
			"a command that rides its own transition must reproduce the owner's "
			+ "state exactly at every acknowledged transition",
		).is_equal(expected[&"position"])


func test_the_lane_needs_no_input_stream_to_find_its_commands() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	# Only the owner lane is delivered. The legacy windowed input never arrives,
	# so a replay that still looked commands up by label would drive nothing.
	for ticks in [1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)

	assert_int(predicted.server_prediction.consumed_count) \
			.override_failure_message(
				"the transition carries its command, so authority needs no "
				+ "second stream to replay it",
			).is_greater(0)
	assert_int(predicted.server_prediction.missing_count).is_equal(0)
	assert_that(predicted.server_root.position).is_not_equal(Vector2.ZERO)


func test_a_resync_declares_every_transition_it_skipped() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.server_prediction.max_consume_lag_ticks = 2
	predicted.client_root.motion = Vector2.RIGHT

	for index in 6:
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
	_step_authority(scenario, predicted)
	_deliver_ack(predicted)

	assert_int(predicted.server_prediction.resync_count).is_equal(1)
	var skipped := predicted.server_prediction.skipped_count
	assert_int(skipped).is_greater(0)
	assert_int(predicted.client_prediction.substituted_count) \
			.override_failure_message(
				"a resync is the one substitution the protocol still admits, so "
				+ "the owner must be told which of its commands never ran",
			).is_equal(skipped)


func test_a_skipped_transition_is_journaled_as_never_driven() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.server_prediction.max_consume_lag_ticks = 2
	predicted.client_root.motion = Vector2.RIGHT

	for index in 6:
		_emit_client_frame(scenario.client_clock, 1)
		_deliver_command(predicted)
	_step_authority(scenario, predicted)

	var journal := predicted.server_prediction.journal()
	var flags := journal.flags()
	var kinds := journal.kinds()
	var substituted := 0
	for i in flags.size():
		if not (flags[i] & NetwPredictJournal.ROW_SUBSTITUTED):
			continue
		substituted += 1
		assert_int(kinds[i]).override_failure_message(
			"authority ran nothing for a skipped transition, so its row must "
			+ "not claim a drive kind that implies it did",
		).is_equal(
			NetwLagCompensationInterface.PredictionHandle.DriveKind.MISSING,
		)
	assert_int(substituted) \
			.is_equal(predicted.server_prediction.skipped_count)


func test_the_ack_lane_reaches_a_verdict_with_no_state_frame() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	# Only the two prediction lanes are delivered. No authoritative state frame
	# ever reaches the owner, so any verdict it reaches came from comparing its
	# own recorded fingerprint against the one authority sent.
	for ticks in [1, 1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	assert_int(predicted.client_prediction.fp_verified_count) \
			.override_failure_message(
				"authority sends the fingerprint of the state its own replay "
				+ "produced, so the owner needs no payload to check itself",
			).is_greater(0)
	assert_int(predicted.client_prediction.fp_mismatch_count) \
			.override_failure_message(
				"both peers ran the same commands from the same state, so every "
				+ "acknowledged transition must fingerprint equal",
			).is_equal(0)
	assert_int(predicted.client_prediction.first_divergent_transition) \
			.is_equal(-1)


func test_the_ack_lane_stays_dense_over_a_long_run() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	# Far past the acknowledgement window and the journal ring, with zero-tick
	# frames mixed in, so a frontier that silently stalls behind one REPEAT
	# entry's row cannot hide in a short all-fresh run.
	for i in 40:
		for ticks in [1, 0, 2, 1, 1]:
			_emit_client_frame(scenario.client_clock, ticks)
			_deliver_command(predicted)
			_step_authority(scenario, predicted)
			_deliver_ack(predicted)

	assert_int(predicted.client_prediction.ack_confirmed_transition) \
			.override_failure_message(
				"the acknowledgement frontier must track the replay, not stall "
				+ "behind it",
			).is_greater_equal(190)
	assert_int(predicted.client_prediction.fp_mismatch_count).is_equal(0)


func test_the_ack_lane_floors_the_command_window() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	var confirmed := predicted.client_prediction.ack_confirmed_transition
	assert_int(confirmed).is_greater_equal(0)
	# The window floor rides the lane that acknowledges transitions, so an
	# acknowledged transition never rides a later command frame.
	var engine := predicted.client_prediction._engine()
	var codecs: Array = engine._input_codecs()
	var frame := PredictFrames.decode_command(
		engine.build_command_frame(),
		codecs[0],
		codecs[1],
	)
	for row: Dictionary in frame.get("transitions", []):
		assert_int(int(row["index"])).override_failure_message(
			"transition %d is acknowledged, so the window must not re-send it"
			% int(row["index"]),
		).is_greater(confirmed)


func test_a_transition_is_judged_once() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)
		# A repeated acknowledgement run is the lane's own redundancy, and a
		# second verdict on a transition already judged would count twice.
		_deliver_ack(predicted)

	var acked := 0
	var flags := predicted.client_prediction.journal().flags()
	for i in flags.size():
		if flags[i] & NetwPredictJournal.ROW_ACKED:
			acked += 1
	assert_int(predicted.client_prediction.fp_verified_count) \
			.override_failure_message(
				"one transition is one verdict, however many times the lane "
				+ "re-sends the acknowledgement that carries it",
			).is_equal(acked)


func test_a_clean_exchange_substitutes_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	assert_int(predicted.client_prediction.substituted_count).is_equal(0)
	assert_int(predicted.server_prediction.resync_count).is_equal(0)
	assert_int(predicted.server_prediction.missing_count).is_equal(0)
