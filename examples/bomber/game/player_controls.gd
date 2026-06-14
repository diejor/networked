extends InputComponent

@export var motion := Vector2():
	set(value):
		# This will be sent by players, make sure values are within limits.
		motion = clamp(value, Vector2(-1, -1), Vector2(1, 1))

@export var bombing: bool = false

@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_left: StringName = "move_left"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_right: StringName = "move_right"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_up: StringName = "move_up"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_down: StringName = "move_down"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var set_bomb: StringName = "set_bomb"


## Returns the action names tracked by this bomber control component.
func get_inputs() -> Array:
	return [
		move_left,
		move_right,
		move_up,
		move_down,
		set_bomb,
	]


## Refreshes [member motion] and [member bombing] from tracked input state each
## tick. Called by [method InputComponent.gather] at
## [signal MultiplayerClock.before_tick] on the controlling client.
func gather() -> void:
	motion = get_vector2(
		move_left,
		move_right,
		move_up,
		move_down,
	)
	bombing = is_down(set_bomb)
