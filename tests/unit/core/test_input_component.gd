## Tests for [InputComponent] and [MoveInputComponent].
class_name TestInputComponent
extends NetwTestSuite

const ACTIONS := [
	&"move_left",
	&"move_right",
	&"move_up",
	&"move_down",
	&"sprint",
]

var comp: MoveInputComponent


func before() -> void:
	for action in ACTIONS:
		if not InputMap.has_action(action):
			InputMap.add_action(action)


func after() -> void:
	for action in ACTIONS:
		if InputMap.has_action(action):
			InputMap.erase_action(action)


func before_test() -> void:
	comp = MoveInputComponent.new()
	add_child(comp)
	auto_free(comp)


func test_initial_state_has_actions_and_defaults() -> void:
	for action in ACTIONS:
		assert_that(comp.state.has(action)).is_true()
	assert_that(comp.is_down(&"move_left")).is_false()
	assert_that(comp.tick_mode).is_false()


func test_get_axis_reports_direction() -> void:
	comp.state[&"move_right"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(1.0)

	comp.state[&"move_right"] = false
	comp.state[&"move_left"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(-1.0)

	comp.state[&"move_right"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(0.0)

	comp.state[&"move_left"] = false
	comp.state[&"move_right"] = false
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(0.0)


func test_get_vector2_reports_normalized_zero_and_cardinal() -> void:
	comp.state[&"move_left"] = true
	comp.state[&"move_up"] = true
	var v := comp.get_vector2(
		&"move_left",
		&"move_right",
		&"move_up",
		&"move_down",
	)
	assert_that(abs(v.length() - 1.0) < 0.001).is_true()
	assert_that(v.x < 0.0).is_true()
	assert_that(v.y < 0.0).is_true()

	comp.state[&"move_left"] = false
	comp.state[&"move_up"] = false
	v = comp.get_vector2(
		&"move_left",
		&"move_right",
		&"move_up",
		&"move_down",
	)
	assert_that(v).is_equal(Vector2.ZERO)

	comp.state[&"move_right"] = true
	v = comp.get_vector2(
		&"move_left",
		&"move_right",
		&"move_up",
		&"move_down",
	)
	assert_that(v).is_equal(Vector2(1.0, 0.0))


func test_on_tick_emits_snapshot_with_tick_state_and_actions() -> void:
	comp.state[&"move_right"] = true
	var container := { "data": { }, "tick": -1 }
	comp.tick_snapshot.connect(
		func(t: int, s: Dictionary) -> void:
			container.tick = t
			container.data = s
	)
	comp._on_tick(0.0, 7)
	assert_that(container.tick).is_equal(7)
	assert_that(
		(container.data as Dictionary).get(&"move_right", false),
	).is_true()
	for action in ACTIONS:
		assert_that((container.data as Dictionary).has(action)).is_true()

	comp.state[&"sprint"] = true
	assert_that(
		(container.data as Dictionary).get(&"sprint", false),
	).is_false()
