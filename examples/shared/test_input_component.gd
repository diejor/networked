class_name TestInputComponent
extends NetwTestSuite

const ACTIONS := [
	&"probe_left",
	&"probe_right",
	&"probe_up",
	&"probe_down",
]


class _Probe extends InputComponent:
	var gather_calls := 0


	func _get_inputs() -> Array:
		return ACTIONS


	func _gather() -> void:
		gather_calls += 1


var comp: _Probe


func before() -> void:
	for action in ACTIONS:
		if not InputMap.has_action(action):
			InputMap.add_action(action)


func after() -> void:
	for action in ACTIONS:
		if InputMap.has_action(action):
			InputMap.erase_action(action)


func before_test() -> void:
	comp = _Probe.new()
	add_child(comp)
	auto_free(comp)


func test_initial_state_has_actions_and_defaults() -> void:
	for action in ACTIONS:
		assert_that(comp.state.has(action)).is_true()
	assert_that(comp.is_down(ACTIONS[0])).is_false()
	assert_that(comp.tick_mode).is_false()


func test_get_axis_reports_direction() -> void:
	comp.state[&"probe_right"] = true
	assert_that(comp.get_axis(&"probe_left", &"probe_right")).is_equal(1.0)

	comp.state[&"probe_right"] = false
	comp.state[&"probe_left"] = true
	assert_that(comp.get_axis(&"probe_left", &"probe_right")).is_equal(-1.0)

	comp.state[&"probe_right"] = true
	assert_that(comp.get_axis(&"probe_left", &"probe_right")).is_equal(0.0)

	comp.state[&"probe_left"] = false
	comp.state[&"probe_right"] = false
	assert_that(comp.get_axis(&"probe_left", &"probe_right")).is_equal(0.0)


func test_get_vector2_reports_normalized_zero_and_cardinal() -> void:
	comp.state[&"probe_left"] = true
	comp.state[&"probe_up"] = true
	var v := comp.get_vector2(
		&"probe_left",
		&"probe_right",
		&"probe_up",
		&"probe_down",
	)
	assert_that(abs(v.length() - 1.0) < 0.001).is_true()
	assert_that(v.x < 0.0).is_true()
	assert_that(v.y < 0.0).is_true()

	comp.state[&"probe_left"] = false
	comp.state[&"probe_up"] = false
	v = comp.get_vector2(
		&"probe_left",
		&"probe_right",
		&"probe_up",
		&"probe_down",
	)
	assert_that(v).is_equal(Vector2.ZERO)

	comp.state[&"probe_right"] = true
	v = comp.get_vector2(
		&"probe_left",
		&"probe_right",
		&"probe_up",
		&"probe_down",
	)
	assert_that(v).is_equal(Vector2(1.0, 0.0))


func test_on_tick_emits_snapshot_with_tick_state_and_actions() -> void:
	comp.state[&"probe_right"] = true
	var container := { "data": { }, "tick": -1 }
	comp.tick_snapshot.connect(
		func(t: int, s: Dictionary) -> void:
			container.tick = t
			container.data = s
	)
	comp._on_tick(0.0, 7)
	assert_that(container.tick).is_equal(7)
	assert_that(
		(container.data as Dictionary).get(&"probe_right", false),
	).is_true()
	for action in ACTIONS:
		assert_that((container.data as Dictionary).has(action)).is_true()

	comp.state[&"probe_up"] = true
	assert_that(
		(container.data as Dictionary).get(&"probe_up", false),
	).is_false()


func test_before_tick_runs_gather_each_tick() -> void:
	comp._on_before_tick(0.0, 3)
	comp._on_before_tick(0.0, 4)
	assert_that(comp.gather_calls).is_equal(2)


func test_action_change_updates_state_and_announces_once() -> void:
	var announced: Array[Array] = []
	comp.action_changed.connect(
		func(action: StringName, pressed: bool) -> void:
			announced.append([action, pressed])
	)

	var press := InputEventAction.new()
	press.action = ACTIONS[0]
	press.pressed = true

	comp._unhandled_input(press)
	assert_that(comp.is_down(ACTIONS[0])).is_true()
	assert_that(announced.size()).is_equal(1)

	comp._unhandled_input(press)
	assert_that(announced.size()).is_equal(1)

	var release := InputEventAction.new()
	release.action = ACTIONS[0]
	release.pressed = false

	comp._unhandled_input(release)
	assert_that(comp.is_down(ACTIONS[0])).is_false()
	assert_that(announced.size()).is_equal(2)
	assert_that(comp.is_down(ACTIONS[1])).is_false()
