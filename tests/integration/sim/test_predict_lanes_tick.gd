## Laws for the [constant Schedule.TICK] tier riding the prediction lanes.
##
## A TICK drive is its own transition, so its tape entry is degenerate: index,
## label, and tick are the same number and every entry is fresh. The laws that
## matter are the same ones the FRAME tier proved: the command lane alone can
## feed authority's consume, the ack lane reaches payload-free verdicts, and
## the one substitution the protocol admits is declared rather than silent.
class_name TestPredictLanesTick
extends NetwTestSuite

const PredictFrames := NetwLagCompensationInterface._PredictFrames


func _emit_client_tick(scenario: PredictionScenario, ticks: int = 1) -> void:
	var clock := scenario.client_clock
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()


func _deliver_command(predicted: PredictedEntity) -> bool:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if bytes.is_empty():
		return false
	predicted.server_prediction._engine().receive_command_frame(bytes)
	return true


# Consumes one authority tick, then records and closes the state it produced
# the way the interface recorder does after the tick, so a journal row is
# closed with the same canonical payload an acknowledgement would fingerprint.
func _step_authority(
		scenario: PredictionScenario,
		predicted: PredictedEntity,
		tick: int,
) -> void:
	predicted.server_prediction.simulate_tick(scenario.dt(), tick)
	var record_tick := predicted.server_prediction.history_record_tick(tick)
	if record_tick < 0:
		return
	var payload := predicted.server_state.canonicalize_payload(
		predicted.server_state.snapshot_payload(),
	)
	predicted.server_entity.timeline.record_state(record_tick, payload)
	predicted.server_prediction._engine().finalize_recorded_state(payload)


func _deliver_ack(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.server_prediction._engine().build_ack_frame()
	if not bytes.is_empty():
		predicted.client_prediction._engine().receive_ack_frame(bytes)


func test_a_tick_drive_authors_its_own_degenerate_transition() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	for i in 4:
		_emit_client_tick(scenario)

	var authored := predicted.client_prediction.tape_transitions()
	assert_array(authored).is_not_empty()
	for entry: Dictionary in authored:
		assert_int(int(entry["index"])).override_failure_message(
			"a TICK transition is its tick, so index and label must be the "
			+ "same number",
		).is_equal(int(entry["label"]))
		assert_bool(bool(entry["fresh"])).is_true()
	assert_int(predicted.client_prediction.last_transition_index) \
			.is_equal(int((authored.back() as Dictionary)["index"]))


func test_a_tick_drive_holds_at_the_structural_ack_age_ceiling() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT
	var ceiling := \
			NetwLagCompensationInterface._PredictionEngine.ACK_AGE_MAX
	for index in ceiling + 3:
		_emit_client_tick(scenario)

	assert_int(predicted.client_prediction.ack_age_ticks).is_equal(ceiling)
	assert_int(predicted.client_prediction.speculation_held_count) \
			.is_greater(0)


func test_authority_replays_the_owner_lane_tick_for_tick() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	for i in 6:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
		_step_authority(scenario, predicted, scenario.client_clock.tick)
		var ack := predicted.server_state.reconcile_ack
		if ack < 0:
			continue
		var expected := predicted.client_prediction.transition_state_at(ack)
		assert_dict(expected).is_not_empty()
		assert_that(predicted.server_root.position).override_failure_message(
			"a command that rides its own transition must reproduce the "
			+ "owner's state exactly at every acknowledged tick",
		).is_equal(expected[&"position"])


func test_the_lane_needs_no_input_stream_to_find_its_commands() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	# Only the owner lane is delivered. The legacy windowed input never arrives,
	# so a consume that still drained the input stream would starve.
	for i in 5:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
		_step_authority(scenario, predicted, scenario.client_clock.tick)

	assert_int(predicted.server_prediction.consumed_count) \
			.override_failure_message(
				"the transition carries its command, so authority needs no "
				+ "second stream to consume it",
			).is_greater(0)
	assert_int(predicted.server_prediction.missing_count).is_equal(0)
	assert_that(predicted.server_root.position).is_not_equal(Vector2.ZERO)


func test_the_ack_lane_reaches_a_verdict_with_no_state_frame() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	# Only the two prediction lanes are delivered. No authoritative state frame
	# ever reaches the owner, so any verdict it reaches came from comparing its
	# own recorded fingerprint against the one authority sent.
	for i in 6:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
		_step_authority(scenario, predicted, scenario.client_clock.tick)
		_deliver_ack(predicted)

	assert_int(predicted.client_prediction.fp_verified_count) \
			.override_failure_message(
				"authority sends the fingerprint of the state its own consume "
				+ "produced, so the owner needs no payload to check itself",
			).is_greater(0)
	assert_int(predicted.client_prediction.fp_mismatch_count) \
			.override_failure_message(
				"both peers ran the same command from the same state, so every "
				+ "acknowledged tick must fingerprint equal",
			).is_equal(0)
	assert_int(predicted.client_prediction.first_divergent_transition) \
			.is_equal(-1)


func test_the_ack_lane_floors_the_command_window() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	for i in 6:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
		_step_authority(scenario, predicted, scenario.client_clock.tick)
		_deliver_ack(predicted)

	var confirmed := predicted.client_prediction.ack_confirmed_transition
	assert_int(confirmed).is_greater_equal(0)
	assert_int(confirmed).is_equal(
		predicted.client_prediction.last_transition_index,
	)
	assert_bool(
		predicted.client_prediction._engine().build_command_frame().is_empty(),
	).override_failure_message(
		"every authored tick is acknowledged, so the window has nothing "
		+ "left to re-send",
	).is_true()


func test_a_tick_resync_declares_every_transition_it_skipped() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.server_prediction.max_consume_lag_ticks = 2
	predicted.client_root.motion = Vector2.RIGHT

	for i in 6:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
	_step_authority(scenario, predicted, scenario.client_clock.tick)
	_deliver_ack(predicted)

	assert_int(predicted.server_prediction.resync_count).is_equal(1)
	var skipped := predicted.server_prediction.skipped_count
	assert_int(skipped).is_greater(0)
	assert_int(predicted.client_prediction.substituted_count) \
			.override_failure_message(
				"a resync is the one substitution the protocol still admits, "
				+ "so the owner must be told which of its ticks never ran",
			).is_equal(skipped)


func test_a_clean_tick_exchange_substitutes_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_root.motion = Vector2.RIGHT

	for i in 6:
		_emit_client_tick(scenario)
		_deliver_command(predicted)
		_step_authority(scenario, predicted, scenario.client_clock.tick)
		_deliver_ack(predicted)

	assert_int(predicted.client_prediction.substituted_count).is_equal(0)
	assert_int(predicted.server_prediction.resync_count).is_equal(0)
	assert_int(predicted.server_prediction.missing_count).is_equal(0)
