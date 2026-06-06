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
	assert_that(a.slot.render_target_update_mode).is_equal(
		SubViewport.UPDATE_DISABLED,
	)


func test_scene_runner_routes_action_to_one_slot() -> void:
	var a := _make_slot_scene("A", false)
	var b := _make_slot_scene("B", true)
	var runner := NetwSceneRunner.new(a.scene, a.slot, &"alice")
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


func _make_slot_scene(label: String, mounted: bool) -> Dictionary:
	var slot := ParticipantSlot.new()
	slot.name = "Slot%s" % label
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
