extends TPLayerAPI


@export var print_debug := false


var api: SceneMultiplayer:
	get:
		return multiplayer
var lobby_manager: MultiplayerLobbyManager:
	get:
		return get_node(api.root_path)


func _ready() -> void:
	if print_debug:
		transition_anim.animation_started.connect(_on_anim_started)
		transition_anim.animation_finished.connect(_on_anim_finished)


func teleport_animation(animation: Callable) -> void:
	lobby_manager.process_mode = Node.PROCESS_MODE_DISABLED
	animation.call()
	await transition_anim.animation_finished
	lobby_manager.process_mode = Node.PROCESS_MODE_INHERIT

func teleport_in() -> void:
	var anim: Callable = transition_anim.play_backwards.bind("tp")
	await teleport_animation(anim)

func teleport_out() -> void:
	var anim: Callable = transition_anim.play.bind("tp")
	await teleport_animation(anim)


func _on_anim_started(name: StringName) -> void:
	print("[ANIM START] ", name,
		" layer_id=", get_instance_id(),
		" player_id=", transition_anim.get_instance_id(),
		" speed_scale=", transition_anim.speed_scale,
		" playing_speed=", transition_anim.get_playing_speed(),
		" pos=", transition_anim.current_animation_position)
	print_stack()

func _on_anim_finished(name: StringName) -> void:
	print("[ANIM FINISH] ", name,
		" speed_scale=", transition_anim.speed_scale,
		" playing_speed=", transition_anim.get_playing_speed(),
		" pos=", transition_anim.current_animation_position)
