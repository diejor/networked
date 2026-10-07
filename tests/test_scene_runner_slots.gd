class_name TestSceneRunnerSlots
extends NetwTestSuite

const ACTION := &"move_right"


class InputProbe extends Node:
	var pressed := false


	func _unhandled_input(event: InputEvent) -> void:
		if event.is_action(ACTION):
			pressed = event.is_action_pressed(ACTION, true)


func before() -> void:
	assert(
		InputMap.has_action(ACTION),
		"InputMap action '%s' must be defined by project.godot." % ACTION,
	)


func after_test() -> void:
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func test_scene_runner_routes_action_to_one_tree() -> void:
	var a := make_tree_scene("A")
	var b := make_tree_scene("B")
	var first := runner_for(a, &"valeria")
	var second := runner_for(b, &"jose")

	second.simulate_action_press(String(ACTION))
	await get_tree().process_frame

	assert_that(b.input.pressed).is_true()
	assert_that(a.input.pressed).is_false()
	assert_that(Input.is_action_pressed(ACTION)).is_false()

	first.simulate_action_press(String(ACTION))
	second.simulate_action_release(String(ACTION))
	await get_tree().process_frame

	assert_that(a.input.pressed).is_true()
	assert_that(b.input.pressed).is_false()
	assert_that(Input.is_action_pressed(ACTION)).is_false()


func test_scene_runner_routes_key_to_one_tree() -> void:
	var runner := runner_for(make_tree_scene("A"), &"valeria")

	runner.simulate_key_press(KEY_A)
	await get_tree().process_frame

	assert_that(runner._key_on_press.has(KEY_A)).is_true()
	assert_that(runner._last_input_event).is_instanceof(InputEventKey)

	runner.simulate_key_release(KEY_A)
	await get_tree().process_frame

	assert_that(runner._key_on_press.has(KEY_A)).is_false()


func test_scene_runner_routes_mouse_button_to_one_tree() -> void:
	var runner := runner_for(make_tree_scene("A"), &"valeria")

	runner.simulate_mouse_button_press(MOUSE_BUTTON_LEFT)
	await get_tree().process_frame

	assert_that(runner._mouse_button_on_press.has(MOUSE_BUTTON_LEFT)).is_true()
	assert_that(runner._last_input_event).is_instanceof(InputEventMouseButton)

	runner.simulate_mouse_button_release(MOUSE_BUTTON_LEFT)
	await get_tree().process_frame

	assert_that(runner._mouse_button_on_press.has(MOUSE_BUTTON_LEFT)).is_false()


func test_scene_runner_reset_input_to_default_clears_tracking() -> void:
	var runner := runner_for(make_tree_scene("A"), &"valeria")

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
	var runner := runner_for(make_tree_scene("A"), &"valeria")

	var time_factor_error := capture_reported_failure(
		func(): return runner.set_time_factor(2.0),
	)
	var simulate_frames_error := capture_reported_failure(
		func(): return runner.simulate_frames(1),
	)

	assert_that(time_factor_error).contains("time scaling is unsupported")
	assert_that(simulate_frames_error).contains("use NetwGameHarness.sync_ticks")


func test_scene_runner_await_player_reports_through_waiter() -> void:
	var runner := runner_for(make_tree_scene("A"), &"valeria")
	var reports: Array[String] = []
	runner.waiter = NetwWaiter.new(
		get_tree(),
		func(label: String, timeout: float) -> void:
			reports.append("%s %.2f" % [label, timeout]),
	)

	var player := await runner.await_player(&"nobody", 0.01)

	assert_that(player).is_null()
	assert_that(reports).contains_exactly(["player 'nobody' in 'valeria' 0.01"])


func capture_reported_failure(callable: Callable) -> String:
	Engine.set_meta(GdUnitConstants.EXPECT_ASSERT_REPORT_FAILURES, true)
	callable.call()
	var failure := GdAssertReports.current_failure()
	Engine.remove_meta(GdUnitConstants.EXPECT_ASSERT_REPORT_FAILURES)
	GdAssertReports.report_success()
	return failure


func runner_for(made: Dictionary, username: StringName) -> NetwSceneRunner:
	var runner := NetwSceneRunner.new(made.scene, made.tree, username)
	auto_free(runner)
	return runner


func make_tree_scene(label: String) -> Dictionary:
	var tree := MultiplayerViewport.new()
	tree.name = "Tree%s" % label
	var scene := Node.new()
	scene.name = "Scene%s" % label
	var input := InputProbe.new()
	scene.add_child(input)
	tree.add_child(scene)
	add_child(tree)
	auto_free(tree)

	return {
		"tree": tree,
		"scene": scene,
		"input": input,
	}
