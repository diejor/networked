## Concrete [TPLayerAPI] that drives an [AnimationPlayer] [code]"tp"[/code] clip and pauses the scene during transitions.
extends TPLayerAPI


var scene_manager: MultiplayerSceneManager:
	get:
		var mt: MultiplayerTree = get_multiplayer_tree()
		if not mt:
			return null
		return mt.get_service(MultiplayerSceneManager)


func _ready() -> void:
	transition_anim.animation_started.connect(_on_anim_started)
	transition_anim.animation_finished.connect(_on_anim_finished)


func teleport_animation(animation: Callable) -> void:
	if scene_manager:
		scene_manager.process_mode = Node.PROCESS_MODE_DISABLED

	animation.call()
	await transition_anim.animation_finished

	if scene_manager:
		scene_manager.process_mode = Node.PROCESS_MODE_INHERIT


func teleport_in() -> void:
	var anim: Callable = transition_anim.play_backwards.bind("tp")
	await teleport_animation(anim)


func teleport_out() -> void:
	var anim: Callable = transition_anim.play.bind("tp")
	await teleport_animation(anim)


func _on_anim_started(name: StringName) -> void:
	_dbg.debug(
		"[ANIM START] %s layer_id=%d player_id=%d speed_scale=%f playing_speed=%f pos=%f" % [
			name, get_instance_id(), transition_anim.get_instance_id(),
			transition_anim.speed_scale, transition_anim.get_playing_speed(),
			transition_anim.current_animation_position
		]
	)


func _on_anim_finished(name: StringName) -> void:
	_dbg.debug(
		"[ANIM FINISH] %s speed_scale=%f playing_speed=%f pos=%f" % [
			name, transition_anim.speed_scale,
			transition_anim.get_playing_speed(),
			transition_anim.current_animation_position
		]
	)
