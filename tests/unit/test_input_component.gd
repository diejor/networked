## Tests InputComponent and MoveInputComponent logic.
##
## These tests manipulate the `state` dictionary directly rather than simulating
## real input events, since GDUnit4 runs headless without an InputMap by default.
## We register temporary actions in before()/after() to satisfy the InputMap asserts.
class_name TestInputComponent
extends GdUnitTestSuite

const ACTIONS := [&"move_left", &"move_right", &"move_up", &"move_down", &"sprint"]

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
	comp = auto_free(MoveInputComponent.new())
	# Manually build state since we're not entering the tree (which would
	# disable processing for non-authority peers).
	comp.state = comp.build_state_dict_from_actions()


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
	var v := comp.get_vector2(&"move_left", &"move_right", &"move_up", &"move_down")
	assert_that(abs(v.length() - 1.0) < 0.001).is_true()
	assert_that(v.x < 0.0).is_true()
	assert_that(v.y < 0.0).is_true()


func test_get_vector2_zero() -> void:
	var v := comp.get_vector2(&"move_left", &"move_right", &"move_up", &"move_down")
	assert_that(v).is_equal(Vector2.ZERO)


func test_get_vector2_cardinal_not_normalized() -> void:
	comp.state[&"move_right"] = true
	var v := comp.get_vector2(&"move_left", &"move_right", &"move_up", &"move_down")
	# Cardinal direction: already unit length, normalization doesn't change it
	assert_that(v).is_equal(Vector2(1.0, 0.0))
