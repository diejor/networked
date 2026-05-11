extends CharacterBody2D


@export_custom(PROPERTY_HINT_NONE, "suffix:px/s") var speed: float = 64


func _physics_process(_delta: float) -> void:
	if not is_multiplayer_authority():
		return
	var dir := Input.get_vector("move_left", "move_right", "move_up", "move_down")
	velocity = dir * speed
	move_and_slide()
