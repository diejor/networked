class_name TPLayer
extends CanvasLayer

signal configured

@export var transition_progress: TextureProgressBar
@export var transition_anim: AnimationPlayer

var pending: NetwPromise
var pending_scene: String = ""
var answered := false
var answered_code: Error = OK
var answered_detail: String = ""
var revealing := false
var presented: NetwSceneHandle


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
	if not session.local_joined.is_connected(on_local_player_joined):
		session.local_joined.connect(on_local_player_joined)
	if not session.presentation_changed.is_connected(on_presentation_changed):
		session.presentation_changed.connect(on_presentation_changed)


func on_local_player_joined(_player: NetwPlayer) -> void:
	await teleport_in()


func on_presentation_changed(
		_from: NetwSceneHandle,
		to: NetwSceneHandle,
) -> void:
	presented = to
	reveal_when_presented()


func is_moving() -> bool:
	return pending != null


func open(scene_path: String) -> NetwPromise:
	answered = false
	pending_scene = scene_path
	pending = NetwPromise.new()
	teleport_out.call_deferred()
	return pending


func answer(code: Error, detail: String) -> void:
	if pending == null:
		return
	answered = true
	answered_code = code
	answered_detail = detail
	reveal_when_presented()


func reveal_when_presented() -> void:
	if pending == null or revealing or not answered:
		return
	if answered_code == OK and not presents(pending_scene):
		return
	revealing = true
	reveal.call_deferred()


func presents(scene_path: String) -> bool:
	if presented == null or not presented.is_declared:
		return false
	return presented.root.scene_file_path == scene_path


func reveal() -> void:
	await teleport_in()
	revealing = false
	var settled := pending
	var code := answered_code
	var detail := answered_detail
	pending = null
	if code == OK:
		settled.resolve(OK)
	else:
		settled.reject(code, detail)


func teleport_animation(animation: Callable) -> void:
	animation.call()
	await transition_anim.animation_finished


func teleport_in() -> void:
	await teleport_animation(transition_anim.play_backwards.bind("tp"))


func teleport_out() -> void:
	await teleport_animation(transition_anim.play.bind("tp"))
