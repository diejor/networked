## Concrete [InputComponent] that tracks four directional movement actions and sprint.
##
## Each action name is configurable via the inspector.  [member direction] and
## [member sprinting] are updated every physics frame and can be read directly.
class_name MoveInputComponent
extends InputComponent

## Input action name for moving left.
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_left: StringName = "move_left"
## Input action name for moving right.
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_right: StringName = "move_right"
## Input action name for moving up.
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_up: StringName = "move_up"
## Input action name for moving down.
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var move_down: StringName = "move_down"
## Input action name for sprinting.
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var sprint: StringName = "sprint"

## Returns all tracked action names by scanning [code]@export_custom[/code] properties with hint [code]"input"[/code].
func get_inputs() -> Array:
	var filter := func(prop: Dictionary) -> bool:
		return prop.hint_string == &"input"
	var map := func (prop: Dictionary) -> StringName:
		return prop.name
	return get_property_list().filter(filter).map(map)


## Normalized movement direction derived from the four directional actions. Updated each physics frame.
var direction: Vector2 = Vector2.ZERO
## Whether the sprint action is currently held. Updated each physics frame.
var sprinting: bool = false


func _physics_process(_delta: float) -> void:
	direction = get_vector2(
		move_left,
		move_right,
		move_up,
		move_down)
	
	sprinting = is_down(sprint)
