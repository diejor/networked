## Tests for [InputComponent] and [MoveInputComponent].
class_name TestInputComponent
extends NetworkedTestSuite

const ACTIONS := [
	&"move_left",
	&"move_right",
	&"move_up",
	&"move_down",
	&"sprint"
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


func test_state_has_all_actions() -> void:
	for action in ACTIONS:
		assert_that(comp.state.has(action)).is_true()


func test_is_down_default_false() -> void:
	assert_that(comp.is_down(&"move_left")).is_false()


func test_get_axis_positive() -> void:
	comp.state[&"move_right"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(1.0)


func test_get_axis_negative() -> void:
	comp.state[&"move_left"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(-1.0)


func test_get_axis_both_cancel() -> void:
	comp.state[&"move_left"] = true
	comp.state[&"move_right"] = true
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(0.0)


func test_get_axis_neither_is_zero() -> void:
	assert_that(comp.get_axis(&"move_left", &"move_right")).is_equal(0.0)


func test_get_vector2_normalized() -> void:
	comp.state[&"move_left"] = true
	comp.state[&"move_up"] = true
	var v := comp.get_vector2(
		&"move_left", &"move_right", &"move_up", &"move_down")
	assert_that(abs(v.length() - 1.0) < 0.001).is_true()
	assert_that(v.x < 0.0).is_true()
	assert_that(v.y < 0.0).is_true()


func test_get_vector2_zero() -> void:
	var v := comp.get_vector2(
		&"move_left", &"move_right", &"move_up", &"move_down")
	assert_that(v).is_equal(Vector2.ZERO)


func test_get_vector2_cardinal_not_normalized() -> void:
	comp.state[&"move_right"] = true
	var v := comp.get_vector2(
		&"move_left", &"move_right", &"move_up", &"move_down")
	assert_that(v).is_equal(Vector2(1.0, 0.0))


func test_on_tick_emits_tick_snapshot_with_correct_tick() -> void:
	var result := {"tick": -1}
	comp.tick_snapshot.connect(
		func(t: int, _s: Dictionary) -> void: result.tick = t)
	comp._on_tick(0.0, 7)
	assert_that(result.tick).is_equal(7)


func test_on_tick_emits_state_snapshot_equal_to_current_state() -> void:
	comp.state[&"move_right"] = true
	var container := {"data": {}}
	comp.tick_snapshot.connect(
		func(_t: int, s: Dictionary) -> void: container.data = s)
	comp._on_tick(0.0, 1)
	assert_that(
		(container.data as Dictionary).get(&"move_right", false)).is_true()


func test_on_tick_snapshot_is_a_copy_not_a_reference() -> void:
	var container := {"data": {}}
	comp.tick_snapshot.connect(
		func(_t: int, s: Dictionary) -> void: container.data = s)
	comp._on_tick(0.0, 1)
	comp.state[&"move_right"] = true
	assert_that(
		(container.data as Dictionary).get(&"move_right", false)).is_false()


func test_on_tick_emits_snapshot_with_all_tracked_actions() -> void:
	var container := {"data": {}}
	comp.tick_snapshot.connect(
		func(_t: int, s: Dictionary) -> void: container.data = s)
	comp._on_tick(0.0, 0)
	for action in ACTIONS:
		assert_that((container.data as Dictionary).has(action)).is_true()


func test_tick_mode_false_by_default() -> void:
	assert_that(comp.tick_mode).is_false()
