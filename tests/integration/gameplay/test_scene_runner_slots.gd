class_name TestSceneRunnerSlots
extends NetwTestSuite

const ACTION := &"move_right"


func before() -> void:
	assert(
		InputMap.has_action(ACTION),
		"InputMap action '%s' must be defined by project.godot." % ACTION,
	)


func after_test() -> void:
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func test_slot_send_input_is_isolated_from_global_input() -> void:
	var a := _make_slot_scene("A", true)
	var b := _make_slot_scene("B", true)

	var event := InputEventAction.new()
	event.action = ACTION
	event.pressed = true
	a.slot.send_input(event)

	await get_tree().process_frame

	assert_that(a.input.state[ACTION]).is_true()
	assert_that(b.input.state[ACTION]).is_false()
	assert_that(Input.is_action_pressed(ACTION)).is_false()


func test_scene_runner_routes_action_to_one_slot() -> void:
	var a := _make_slot_scene("A", false)
	var b := _make_slot_scene("B", true)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	auto_free(runner)

	runner.simulate_action_press(String(ACTION))
	await get_tree().process_frame

	assert_that(a.input.state[ACTION]).is_true()
	assert_that(b.input.state[ACTION]).is_false()
	assert_that(Input.is_action_pressed(ACTION)).is_false()

	runner.simulate_action_release(String(ACTION))
	await get_tree().process_frame

	assert_that(a.input.state[ACTION]).is_false()
	assert_that(Input.is_action_pressed(ACTION)).is_false()


# These tests reach into base class private state. They pin the
# GdUnitSceneRunnerImpl contract that NetwSceneRunner wraps.
func test_scene_runner_routes_key_to_one_slot() -> void:
	var a := _make_slot_scene("A", false)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	auto_free(runner)

	runner.simulate_key_press(KEY_A)
	await get_tree().process_frame

	assert_that(runner._key_on_press.has(KEY_A)).is_true()
	assert_that(runner._last_input_event).is_instanceof(InputEventKey)

	runner.simulate_key_release(KEY_A)
	await get_tree().process_frame

	assert_that(runner._key_on_press.has(KEY_A)).is_false()


func test_scene_runner_routes_mouse_button_to_one_slot() -> void:
	var a := _make_slot_scene("A", false)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	auto_free(runner)

	runner.simulate_mouse_button_press(MOUSE_BUTTON_LEFT)
	await get_tree().process_frame

	assert_that(runner._mouse_button_on_press.has(MOUSE_BUTTON_LEFT)).is_true()
	assert_that(runner._last_input_event).is_instanceof(InputEventMouseButton)

	runner.simulate_mouse_button_release(MOUSE_BUTTON_LEFT)
	await get_tree().process_frame

	assert_that(runner._mouse_button_on_press.has(MOUSE_BUTTON_LEFT)).is_false()


func test_scene_runner_reset_input_to_default_clears_tracking() -> void:
	var a := _make_slot_scene("A", false)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	auto_free(runner)

	runner.simulate_action_press(String(ACTION))
	runner.simulate_key_press(KEY_A)
	runner.simulate_mouse_button_press(MOUSE_BUTTON_LEFT)

	assert_that(runner._action_on_press.is_empty()).is_false()
	assert_that(runner._key_on_press.is_empty()).is_false()
	assert_that(runner._mouse_button_on_press.is_empty()).is_false()

	runner._reset_input_to_default()

	assert_that(runner._action_on_press.is_empty()).is_true()
	assert_that(runner._key_on_press.is_empty()).is_true()
	assert_that(runner._mouse_button_on_press.is_empty()).is_true()
	assert_that(runner._last_input_event).is_null()


func test_scene_runner_reports_harness_owned_time_footguns() -> void:
	var a := _make_slot_scene("A", false)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	auto_free(runner)

	var time_factor_error := _capture_reported_failure(
		func(): return runner.set_time_factor(2.0),
	)
	var simulate_frames_error := _capture_reported_failure(
		func(): return runner.simulate_frames(1),
	)

	assert_that(time_factor_error).contains("use NetwGameHarness.set_time_factor")
	assert_that(simulate_frames_error).contains("use NetwGameHarness.sync_ticks")


func test_scene_runner_await_player_reports_through_waiter() -> void:
	var a := _make_slot_scene("A", false)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"valeria")
	var reports: Array[String] = []
	auto_free(runner)
	runner.waiter = NetwWaiter.new(
		get_tree(),
		func(label: String, timeout: float) -> void:
			reports.append("%s %.2f" % [label, timeout]),
	)

	var player := await runner.await_player(&"nobody", 0.01)

	assert_that(player).is_null()
	assert_that(reports).contains_exactly(["player 'nobody' in 'valeria' 0.01"])


func _capture_reported_failure(callable: Callable) -> String:
	Engine.set_meta(GdUnitConstants.EXPECT_ASSERT_REPORT_FAILURES, true)
	callable.call()
	var failure := GdAssertReports.current_failure()
	Engine.remove_meta(GdUnitConstants.EXPECT_ASSERT_REPORT_FAILURES)
	GdAssertReports.report_success()
	return failure


func _make_slot_scene(label: String, mounted: bool) -> Dictionary:
	var slot := ParticipantWindow.new()
	slot.name = "Window%s" % label
	add_child(slot)
	auto_free(slot)

	var scene := Node.new()
	scene.name = "Scene%s" % label
	var input := MoveInputComponent.new()
	input.name = "InputComponent"
	scene.add_child(input)
	if mounted:
		slot.add_child(scene)

	return {
		"slot": slot,
		"scene": scene,
		"input": input,
	}
