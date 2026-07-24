class_name TestPredictionTape
extends NetwTestSuite

func test_frame_authoring_clamps_zero_and_folds_double_tick_frames() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_prediction.schedule().frame()
	predicted.client_root.motion = Vector2.RIGHT

	_emit_client_frame(scenario.client_clock, 1)
	_emit_client_frame(scenario.client_clock, 0)
	_emit_client_frame(scenario.client_clock, 2)
	_emit_client_frame(scenario.client_clock, 0)

	var entries := predicted.client_prediction.tape_transitions()
	assert_int(entries.size()).is_equal(2)
	assert_array(entries.map(func(entry): return entry["index"])) \
			.is_equal([0, 1])
	assert_array(entries.map(func(entry): return entry["fresh"])) \
			.is_equal([true, true])
	var first_label := int(entries[0]["label"])
	assert_array(entries.map(func(entry): return entry["label"])) \
			.is_equal([first_label, first_label + 2])
	assert_bool(
		entries.any(
			func(entry): return int(entry["label"]) == first_label + 1
		),
	).override_failure_message(
		"a folded double-tick label must not author a tape entry",
	).is_false()

	var first_state := predicted.client_prediction.transition_state_at(0)
	var folded_state := predicted.client_prediction.transition_state_at(1)
	assert_dict(first_state).is_not_empty()
	assert_dict(folded_state).is_not_empty()
	assert_that(folded_state[&"position"]).is_not_equal(
		first_state[&"position"],
	)
	assert_int(predicted.client_prediction.authoring_clamped_count).is_equal(2)


func test_command_lane_deduplicates_redundancy_overlap_on_server() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 0, 2, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	_deliver_command(predicted)
	assert_int(predicted.server_prediction.tape_epoch).is_equal(
		predicted.client_prediction.tape_epoch,
	)

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var client_entries := predicted.client_prediction.tape_transitions()
	_deliver_command(predicted)

	var server_entries := predicted.server_prediction.tape_transitions()
	assert_array(server_entries).is_equal(client_entries)
	assert_int(predicted.server_prediction.tape_queue_depth).is_equal(6)
	assert_int(predicted.server_prediction.consumed_count) \
			.override_failure_message(
				"lane receipt must not replay before the frame step",
			).is_equal(0)


func _deliver_command(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()
