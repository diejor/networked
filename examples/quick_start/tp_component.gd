class_name TPComponent
extends Node

signal teleport_committed

@export_file("*.tscn") var current_scene_path: String = ""
@export var settle_seconds: float = 0.5

var settle_until_msec: int = 0
var pending: NetwPromise
var pending_marker: NodePath
var layer: TPLayer

var is_moving: bool:
	get:
		return pending != null

@onready var entity := NetwEntity.of(owner)
@onready var session: NetwSessionHandle = Netw.session(self)


func _init() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		Netw.configure_property(self, &"current_scene_path") \
				.on_spawn().persisted()


func _ready() -> void:
	entity.reparented.connect(apply_pending_marker)
	if is_multiplayer_authority():
		layer = Netw.service(self, TPLayer)


func is_settling() -> bool:
	return Time.get_ticks_msec() < settle_until_msec


func teleport(target_scene: String, target_marker: NodePath) -> NetwPromise:
	if is_moving:
		return pending
	if is_settling():
		return NetwPromise.resolved(OK)
	pending = NetwPromise.new()
	cover_and_request.call_deferred(target_scene, target_marker)
	return pending


func cover_and_request(target_scene: String, target_marker: NodePath) -> void:
	await layer.teleport_out()
	request_teleport.rpc_id(1, target_scene, target_marker)


@rpc("authority", "call_local", "reliable")
func request_teleport(scene_path: String, marker_path: NodePath) -> void:
	if not multiplayer.is_server():
		return
	var recipe := load(scene_path) as PackedScene
	var destination: NetwSceneHandle = session.activate_scene(recipe)
	var marker := destination.root.get_node_or_null(marker_path) as Node2D
	if marker == null:
		complete_teleport.rpc_id(
			owner.get_multiplayer_authority(),
			ERR_DOES_NOT_EXIST,
			"Teleport destination has no marker at '%s'." % marker_path,
		)
		return
	current_scene_path = scene_path
	prepare_teleport.rpc(marker_path)
	apply_marker(marker)
	var moved := destination.move(entity)
	moved.then(
		func(_value: Variant) -> void:
			complete_teleport.rpc_id(owner.get_multiplayer_authority())
	)
	moved.catch_error(
		func(code: Error, detail: String) -> void:
			complete_teleport.rpc_id(owner.get_multiplayer_authority(), code, detail)
	)


@rpc("any_peer", "call_local", "reliable")
func prepare_teleport(marker_path: NodePath) -> void:
	if multiplayer.get_remote_sender_id() == 1:
		pending_marker = marker_path


func apply_pending_marker() -> void:
	if not pending_marker.is_empty():
		apply_marker(entity.scene.root.get_node(pending_marker))


func apply_marker(marker: Node2D) -> void:
	(owner as Node2D).global_position = marker.global_position
	entity.interpolation.reset()
	owner.reset_physics_interpolation()


@rpc("any_peer", "call_local", "reliable")
func complete_teleport(code: Error = OK, detail: String = "") -> void:
	if multiplayer.get_remote_sender_id() != 1:
		return
	if code == OK:
		apply_pending_marker()
		Netw.sync_property(owner, &"position")
		finish_teleport.rpc()
	else:
		pending_marker = NodePath()
	await layer.teleport_in()
	var completed := pending
	pending = null
	settle_until_msec = Time.get_ticks_msec() + int(settle_seconds * 1000.0)
	if code == OK:
		completed.resolve(OK)
	else:
		completed.reject(code, detail)


@rpc("authority", "call_local", "reliable")
func finish_teleport() -> void:
	entity.interpolation.reset()
	owner.reset_physics_interpolation()
	pending_marker = NodePath()
	if multiplayer.is_server():
		entity.persistence.flush()
	teleport_committed.emit()


func spawn(host: Node) -> void:
	var recipe := load(current_scene_path) as PackedScene
	var destination: NetwSceneHandle = Netw.session(host).activate_scene(recipe)
	destination.add_player(NetwEntity.of(owner))
