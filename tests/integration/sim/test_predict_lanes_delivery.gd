## Delivery laws for the prediction owner lane.
##
## These cases carry the frames between the two live engines the way the landed
## sync-frame laws carry a frame between two bindings, which keeps the schedule
## exact and leaves link behavior to the capture. Each law hands authority the
## owner's frames in a shape the lane's redundancy must absorb: duplicates,
## gaps, and overlap.
##
## The rule they exist for is the one the old grammar could not state: a fresh
## transition and its command are inseparable, so authority never holds a
## transition it would have to invent a command for.
class_name TestPredictLanesDelivery
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME


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


# Carries one owner frame across, returning whether authority admitted it.
func _deliver_command(predicted: PredictedEntity) -> bool:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if bytes.is_empty():
		return false
	predicted.server_prediction._engine().receive_command_frame(bytes)
	return true


func test_the_owner_lane_carries_every_authored_transition() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 0, 2, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		assert_bool(_deliver_command(predicted)).is_true()

	var authored := predicted.client_prediction.tape_transitions()
	assert_int(predicted.server_prediction.command_queue_depth) \
			.override_failure_message(
				"every authored transition must reach authority with its command",
			).is_equal(authored.size())
	assert_int(predicted.server_prediction.frames_dropped_invalid).is_equal(0)


func test_redundant_sends_deduplicate_instead_of_accumulating() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
	var depth := predicted.server_prediction.command_queue_depth

	# Every send repeats the range still in flight. Re-delivering the same frame
	# must be a no-op, since that redundancy is what heals a lost datagram.
	for repeat in 3:
		_deliver_command(predicted)

	assert_int(predicted.server_prediction.command_queue_depth) \
			.override_failure_message(
				"redundancy overlap must deduplicate, never stack",
			).is_equal(depth)
	assert_int(predicted.server_prediction.frames_dropped_invalid).is_equal(0)


func test_a_dropped_owner_frame_heals_on_the_next_send() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	# Author two more transitions whose frames never arrive.
	_emit_client_frame(scenario.client_clock, 1)
	_emit_client_frame(scenario.client_clock, 1)
	assert_int(predicted.server_prediction.command_queue_depth).is_equal(1)

	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)

	assert_int(predicted.server_prediction.command_queue_depth) \
			.override_failure_message(
				"the window repeats what is still in flight, so one arrival "
				+ "heals the gap without a retransmit",
			).is_equal(4)


func test_authority_never_holds_a_transition_without_its_command() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)

	var engine := predicted.server_prediction._engine()
	for transition in predicted.server_prediction.command_queue_depth:
		var queued: Dictionary = engine._command_queue[transition]
		if not bool(queued["fresh"]):
			continue
		assert_dict(queued["command"]).override_failure_message(
			"transition %d is fresh, so authority must hold the command that "
			% transition + "drove it rather than one it substituted",
		).is_not_empty()


func test_a_clean_exchange_declares_no_substitution() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
		_deliver_command(predicted)
		predicted.server_prediction.simulate_frame(scenario.dt())
		var ack: PackedByteArray = \
				predicted.server_prediction._engine().build_ack_frame()
		if not ack.is_empty():
			predicted.client_prediction._engine().receive_ack_frame(ack)

	assert_int(predicted.client_prediction.substituted_count) \
			.override_failure_message(
				"authority that received every command substitutes none",
			).is_equal(0)
	assert_int(predicted.client_prediction.frames_dropped_invalid).is_equal(0)
