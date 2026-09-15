class_name TPLayer
extends CanvasLayer

signal configured

const ARRIVAL_TOLERANCE := 1.0

@export var transition_progress: TextureProgressBar
@export var transition_anim: AnimationPlayer
@export var settle_seconds: float = 0.5

var pending: NetwPromise
var pending_scene: String = ""
var pending_marker: NodePath
var answered := false
var answered_code: Error = OK
var answered_detail: String = ""
var revealing := false
var settle_until_msec: int = 0


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


func is_moving() -> bool:
	return pending != null


func is_settling() -> bool:
	return Time.get_ticks_msec() < settle_until_msec


func open(scene_path: String, marker_path: NodePath) -> NetwPromise:
	answered = false
	pending_scene = scene_path
	pending_marker = marker_path
	pending = NetwPromise.new()
	teleport_out.call_deferred()
	return pending


func answer(code: Error, detail: String) -> void:
	if pending == null:
		return
	answered = true
	answered_code = code
	answered_detail = detail
	reveal_when_placed()


func placed() -> void:
	reveal_when_placed()


func reveal_when_placed() -> void:
	if pending == null or revealing or not answered:
		return
	if answered_code == OK and not arrived():
		return
	revealing = true
	reveal.call_deferred()


func arrived() -> bool:
	var entity := local_entity()
	if entity == null:
		return false
	var scene: NetwSceneHandle = entity.scene
	if scene == null or not scene.is_declared:
		return false
	if scene.root.scene_file_path != pending_scene:
		return false
	var marker := scene.root.get_node_or_null(pending_marker) as Node2D
	if marker == null:
		return false
	var body := entity.owner as Node2D
	return body.global_position.distance_to(marker.global_position) \
			< ARRIVAL_TOLERANCE


func local_entity() -> NetwEntity:
	var session: NetwSessionHandle = Netw.session(self)
	if session == null:
		return null
	var who: NetwParticipant = session.local_participant
	if who == null:
		return null
	var mine: Array[NetwEntity] = []
	for player: NetwEntity in who.players:
		if is_instance_valid(player.owner):
			mine.append(player)
	return mine[0] if mine.size() == 1 else null


func reveal() -> void:
	await teleport_in()
	revealing = false
	var settled := pending
	var code := answered_code
	var detail := answered_detail
	pending = null
	settle_until_msec = Time.get_ticks_msec() + int(settle_seconds * 1000.0)
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
