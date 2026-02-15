class_name MoveInputComponent
extends InputComponent

## Inherited from InputComponent
@onready var actions: PlayerActions = _actions

var direction: Vector2 = Vector2.ZERO
var sprinting: bool = false


func _physics_process(_delta: float) -> void:
	direction = get_vector2(
		actions.move_left,
		actions.move_right,
		actions.move_up,
		actions.move_down)
	
	sprinting = is_down(actions.sprint)
