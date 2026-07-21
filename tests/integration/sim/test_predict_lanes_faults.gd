## Laws for the prediction lanes under a link that loses, reorders, and repeats.
##
## The closure the protocol promises is that a transition never runs without the
## command that belongs to it. Delivery is what tests that promise, so these laws
## hand authority the owner's frames in orders a real link would produce and
## assert the replayed state still reproduces the owner's own prediction exactly.
class_name TestPredictLanesFaults
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule = FRAME
	predicted.server_prediction.schedule = FRAME
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.server_prediction.missing_policy = \
	NetwLagCompensationInterface.PredictionHandle.MissingInput.REPEAT_LAST


# Authors one owner frame and returns the bytes the lane would ship, without
# handing them to authority. Holding the bytes is what lets a law choose the
# delivery order.
func _author(
		scenario: PredictionScenario,
		predicted: PredictedEntity,
) -> PackedByteArray:
	scenario.client_clock.before_tick_loop.emit()
	scenario.client_clock.force_step(1)
	scenario.client_clock.after_tick_loop.emit()
	return predicted.client_prediction._engine().build_command_frame()


func _deliver(predicted: PredictedEntity, bytes: PackedByteArray) -> void:
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


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


# The owner's own prediction is the ground truth authority must reproduce.
# The newest replayed transition is derived from the replay count because a
# trailing held pass stamps the no-ack sentinel on the state binding, and these
# rigs replay a fresh engine's entries in order from zero.
func _assert_replay_matches_owner(predicted: PredictedEntity) -> void:
	var ack := predicted.server_prediction.consumed_count - 1
	assert_int(ack).override_failure_message(
		"authority must have replayed something to be checked at all",
	).is_greater_equal(0)
	var expected := predicted.client_prediction.transition_state_at(ack)
	assert_dict(expected).is_not_empty()
	assert_that(predicted.server_root.position).override_failure_message(
		"a transition carries its own command, so however the frames arrived "
		+ "authority must land on the state the owner predicted",
	).is_equal(expected[&"position"])


func test_redundancy_repairs_a_dropped_frame() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for index in 6:
		var bytes := _author(scenario, predicted)
		# The third frame never arrives. The window carries its transitions
		# again in the frames that follow, so nothing is lost.
		if index != 2:
			_deliver(predicted, bytes)
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	assert_int(predicted.server_prediction.missing_count) \
			.override_failure_message(
				"a lost frame is repaired by the window that re-sends it, so "
				+ "authority never has to invent a command",
			).is_equal(0)
	assert_int(predicted.client_prediction.substituted_count).is_equal(0)
	_assert_replay_matches_owner(predicted)


func test_a_reordered_and_repeated_delivery_changes_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	var frames: Array[PackedByteArray] = []
	for index in 5:
		frames.append(_author(scenario, predicted))

	# A link that swaps neighbours and repeats what it already delivered. The
	# queue admits a transition once and replays in order, so authority holds
	# for the gap rather than running the later transition early.
	for index in [0, 2, 1, 1, 4, 3, 3]:
		_deliver(predicted, frames[index])
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	assert_int(predicted.server_prediction.missing_count).is_equal(0)
	assert_int(predicted.client_prediction.substituted_count).is_equal(0)
	assert_int(predicted.client_prediction.fp_mismatch_count) \
			.override_failure_message(
				"delivery order is not an antecedent of the recurrence, so it "
				+ "cannot move a fingerprint",
			).is_equal(0)
	_assert_replay_matches_owner(predicted)


func test_a_frame_that_drove_nothing_confirms_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for index in 3:
		_deliver(predicted, _author(scenario, predicted))
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	var consumed := predicted.server_prediction.consumed_count - 1
	assert_int(predicted.client_prediction.ack_confirmed_transition) \
			.is_greater_equal(0)

	# Authority runs three more frames with nothing to replay. Its confirmation
	# frontier may still settle onto the last transition it consumed, because a
	# row closes on the frame after the drive that produced its state, but it can
	# never pass that transition onto one whose command never arrived.
	for index in 3:
		_step_authority(scenario, predicted)
		_deliver_ack(predicted)

	assert_int(predicted.server_prediction.consumed_count - 1) \
			.override_failure_message(
				"a frame with nothing to replay consumes nothing",
			).is_equal(consumed)
	assert_int(predicted.client_prediction.ack_confirmed_transition) \
			.override_failure_message(
				"authority acknowledges what it ran, so its frontier settles at "
				+ "the last transition it consumed and stops there",
			).is_equal(consumed)
	assert_int(predicted.client_prediction.substituted_count).is_equal(0)


func test_a_starved_authority_declares_what_it_skipped() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.server_prediction.max_consume_lag_ticks = 2
	predicted.client_root.motion = Vector2.RIGHT

	# The owner authors far ahead of what authority drains, so authority must
	# resync and abandon transitions it will never run.
	for index in 8:
		_deliver(predicted, _author(scenario, predicted))
	_step_authority(scenario, predicted)
	_deliver_ack(predicted)

	var skipped := predicted.server_prediction.skipped_count
	assert_int(skipped).is_greater(0)
	assert_int(predicted.client_prediction.substituted_count) \
			.override_failure_message(
				"every transition a resync abandons must reach the owner as a "
				+ "declared substitution, never as a silent gap",
			).is_equal(skipped)

	# A substituted transition is closed without a state, so the owner must not
	# charge it as a fingerprint divergence.
	assert_int(predicted.client_prediction.fp_mismatch_count) \
			.override_failure_message(
				"a transition that never ran has no state to disagree with",
			).is_equal(0)
