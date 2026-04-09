extends CharacterBody2D


@export var speed: float = 64


func _physics_process(_delta: float) -> void:
	if not is_multiplayer_authority() or multiplayer.is_server():
		return
	var dir := Input.get_vector("move_left", "move_right", "move_up", "move_down")
	velocity = dir * speed
	move_and_slide()
