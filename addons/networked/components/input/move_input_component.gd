class_name MoveInputComponent
extends InputComponent

@export_custom(PROPERTY_HINT_INPUT_NAME, &"input") 
var move_left: StringName = "move_left"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input") 
var move_right: StringName = "move_right"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input") 
var move_up: StringName = "move_up"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input") 
var move_down: StringName = "move_down"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var sprint: StringName = "sprint"

func get_inputs() -> Array:
	var filter := func(prop: Dictionary) -> bool:
		return prop.hint_string == &"input"
	var map := func (prop: Dictionary) -> StringName:
		return prop.name
	return get_property_list().filter(filter).map(map)


var direction: Vector2 = Vector2.ZERO
var sprinting: bool = false


func _physics_process(_delta: float) -> void:
	direction = get_vector2(
		move_left,
		move_right,
		move_up,
		move_down)
	
	sprinting = is_down(sprint)
