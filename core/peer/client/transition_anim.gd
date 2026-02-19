class_name TransitionPlayer
extends AnimationPlayer

func teleport_animation(animation: Callable) -> void:
	owner.process_mode = Node.PROCESS_MODE_DISABLED
	animation.call()
	await animation_finished
	owner.process_mode = Node.PROCESS_MODE_INHERIT

func teleport_in_animation() -> void:
	var anim: Callable = play_backwards.bind("tp")
	await teleport_animation(anim)

func teleport_out_animation() -> void:
	var anim: Callable = play.bind("tp")
	await teleport_animation(anim)
