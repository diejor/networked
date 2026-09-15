class_name TPComponent
extends Node

signal teleport_committed

@export_file("*.tscn") var current_scene_path: String = ""

var arrival_marker: NodePath
var layer: TPLayer

var is_moving: bool:
	get:
		return layer.is_moving()

@onready var entity := NetwEntity.of(owner)
@onready var session: NetwSessionHandle = Netw.session(self)


func _init() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		Netw.configure_property(self, &"current_scene_path") \
				.on_spawn().persisted()


func _ready() -> void:
	entity.reparented.connect(apply_arrival_marker)
	layer = Netw.service(self, TPLayer)
	if is_multiplayer_authority():
		layer.placed()


func is_settling() -> bool:
	return layer.is_settling()


func teleport(target_scene: String, target_marker: NodePath) -> NetwPromise:
	if layer.is_moving():
		return layer.pending
	if layer.is_settling():
		return NetwPromise.resolved(OK)
	var opened := layer.open(target_scene, target_marker)
	request_teleport.rpc_id(1, target_scene, target_marker)
	return opened


@rpc("authority", "call_local", "reliable")
func request_teleport(scene_path: String, marker_path: NodePath) -> void:
	if not multiplayer.is_server():
		return
	var mover := owner.get_multiplayer_authority()
	var recipe := load(scene_path) as PackedScene
	var destination: NetwSceneHandle = session.activate_scene(recipe)
	var marker := destination.root.get_node_or_null(marker_path) as Node2D
	if marker == null:
		complete_teleport.rpc_id(
			mover,
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
			complete_teleport.rpc_id(mover)
	)
	moved.catch_error(
		func(code: Error, detail: String) -> void:
			complete_teleport.rpc_id(mover, code, detail)
	)


@rpc("any_peer", "call_local", "reliable")
func prepare_teleport(marker_path: NodePath) -> void:
	if multiplayer.get_remote_sender_id() == 1:
		arrival_marker = marker_path


func apply_arrival_marker() -> void:
	if not arrival_marker.is_empty():
		apply_marker(entity.scene.root.get_node(arrival_marker))


func apply_marker(marker: Node2D) -> void:
	(owner as Node2D).global_position = marker.global_position
	entity.interpolation.reset()
	owner.reset_physics_interpolation()
	if is_multiplayer_authority():
		layer.placed()


@rpc("any_peer", "call_local", "reliable")
func complete_teleport(code: Error = OK, detail: String = "") -> void:
	if multiplayer.get_remote_sender_id() != 1:
		return
	if code == OK:
		apply_arrival_marker()
		Netw.sync_property(owner, &"position")
		finish_teleport.rpc()
	else:
		arrival_marker = NodePath()
	layer.answer(code, detail)


@rpc("authority", "call_local", "reliable")
func finish_teleport() -> void:
	entity.interpolation.reset()
	owner.reset_physics_interpolation()
	arrival_marker = NodePath()
	if multiplayer.is_server():
		entity.persistence.flush()
	teleport_committed.emit()
