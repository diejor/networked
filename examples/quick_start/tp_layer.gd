class_name TPLayer
extends CanvasLayer

signal configured

@export var transition_progress: TextureProgressBar
@export var transition_anim: AnimationPlayer


func _init() -> void:
	configured.connect(on_multiplayer_configured)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	NetwService.register(self, TPLayer)
	var session: NetwSessionHandle = Netw.session(self)
	if session and not session.entered.is_connected(configured.emit):
		session.entered.connect(configured.emit)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var session: NetwSessionHandle = Netw.session(self)
	if session and session.entered.is_connected(configured.emit):
		session.entered.disconnect(configured.emit)
	NetwService.unregister(self, TPLayer)


func on_multiplayer_configured() -> void:
	var session: NetwSessionHandle = Netw.session(self)
	if session == null:
		return
	if session.role == NetwMultiplayer.ROLE_DEDICATED_SERVER:
		queue_free()
		return
	if not session.local_joined.is_connected(on_local_participant_joined):
		session.local_joined.connect(on_local_participant_joined)


func on_local_participant_joined(_participant: NetwParticipant) -> void:
	await teleport_in()


func teleport_animation(animation: Callable) -> void:
	animation.call()
	await transition_anim.animation_finished


func teleport_in() -> void:
	await teleport_animation(transition_anim.play_backwards.bind("tp"))


func teleport_out() -> void:
	await teleport_animation(transition_anim.play.bind("tp"))
