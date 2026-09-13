class_name TPComponent
extends Node

signal teleport_committed

@export_file("*.tscn") var starting_scene: String = ""

@export var current_scene_path: String = "":
	get:
		return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value

@export var settle_seconds: float = 0.5

var settle_until_msec: int = 0
var is_moving: bool = false
var pending_marker_path: String = ""

var current_scene_name: String:
	get:
		return resolve_scene_name(current_scene_path)


class Bucket extends RefCounted:
	var pending: Dictionary[int, NetwPromise] = { }


func _init() -> void:
	name = "TPComponent"
	unique_name_in_owner = true
	process_mode = Node.PROCESS_MODE_ALWAYS


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED or Engine.is_editor_hint():
		return
	var entity := Netw.configure_entity(self)
	if not entity or not entity.owner:
		return
	if not entity.reparented.is_connected(apply_pending_marker):
		entity.reparented.connect(apply_pending_marker)
	Netw.configure_property(self, &"current_scene_path").on_spawn().persisted()


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	ensure_current_scene_path()


func ensure_current_scene_path() -> void:
	if current_scene_path.is_empty() and not starting_scene.is_empty():
		current_scene_path = starting_scene


static func resolve_scene_recipe(path_or_uid: String) -> PackedScene:
	if path_or_uid.is_empty():
		return null
	var path: String = ResourceUID.ensure_path(path_or_uid)
	if not ResourceLoader.exists(path):
		push_error("Unable to find scene at path '%s'." % path)
		return null
	var scene: PackedScene = load(path)
	if not is_instance_valid(scene):
		push_error("Unable to find scene at path '%s'." % path)
		return null
	return scene


static func resolve_scene_name(path_or_uid: String) -> String:
	var scene := resolve_scene_recipe(path_or_uid)
	return scene.get_state().get_node_name(0) if scene else StringName()


func peer_bucket() -> Bucket:
	var session: NetwSessionHandle = Netw.session(self)
	if not session:
		return null
	return session.bucket_of(multiplayer.get_unique_id(), Bucket) as Bucket


func tp_layer() -> TPLayer:
	if not is_inside_tree() or not multiplayer:
		return null
	if not Netw.session(self).is_local_client:
		return null
	return Netw.service(self, TPLayer) as TPLayer


func is_settling() -> bool:
	return Time.get_ticks_msec() < settle_until_msec


func teleport(target_scene: String, target_marker: NodePath) -> NetwPromise:
	var promise := NetwPromise.new()
	if is_moving or is_settling():
		promise.resolve(OK)
		return promise
	is_moving = true
	var bucket := peer_bucket()
	if bucket:
		bucket.pending[multiplayer.get_unique_id()] = promise
	@warning_ignore("missing_await")
	cover_and_request(target_scene, target_marker)
	return promise


func cover_and_request(target_scene: String, target_marker: NodePath) -> void:
	var layer := tp_layer()
	if layer:
		await layer.teleport_out()
	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		target_scene,
		String(target_marker),
	)


@rpc("any_peer", "call_local", "reliable")
func request_teleport(to_scene_path: String, tp_path: String) -> void:
	if not multiplayer.is_server():
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if sender_id != owner.get_multiplayer_authority():
		push_warning(
			"teleport rejected: peer %d is not authority %d for '%s'"
			% [sender_id, owner.get_multiplayer_authority(), owner.name],
		)
		return

	var recipe := resolve_scene_recipe(to_scene_path)
	if recipe == null:
		return
	var destination_name := recipe.get_state().get_node_name(0)
	var destination: NetwSceneHandle = Netw.session(self).activate_scene(recipe)
	if not destination:
		push_error("Destination scene '%s' could not be activated." % destination_name)
		return
	var level: Node = destination.root

	current_scene_path = to_scene_path

	var marker := level.get_node_or_null(NodePath(tp_path)) as Node2D
	var authority := owner.get_multiplayer_authority()
	if marker == null:
		var detail := "Destination '%s' has no Node2D marker at '%s'." % [
			destination_name,
			tp_path,
		]
		push_error(detail)
		fail_teleport.rpc_id(authority, ERR_DOES_NOT_EXIST, detail)
		return
	prepare_teleport.rpc(tp_path)
	apply_marker(marker)
	var opts := NetwReparentOpts.new()
	opts.reason = &"teleport"

	if owner.get_parent() == level:
		commit_teleport.rpc_id(authority)
		return
	var moved := destination.move(NetwEntity.of(owner), opts)
	moved.then(
		func(_value: Variant) -> void:
			commit_teleport.rpc_id(authority)
	)
	moved.catch_error(
		func(code: Error, detail: String) -> void:
			var message := "Teleport of '%s' to '%s' failed. %s" % [
				owner.name,
				destination_name,
				error_string(code) if detail.is_empty() else detail,
			]
			push_error(message)
			fail_teleport.rpc_id(authority, code, message)
	)


@rpc("any_peer", "call_local", "reliable")
func prepare_teleport(marker_path: String) -> void:
	if multiplayer.get_remote_sender_id() != 1:
		return
	pending_marker_path = marker_path


func apply_pending_marker() -> void:
	if pending_marker_path.is_empty():
		return
	var level: Node = NetwEntity.of(owner).scene.root
	var marker := level.get_node_or_null(NodePath(pending_marker_path)) as Node2D
	if marker == null:
		push_error(
			"Arrival scene '%s' has no Node2D marker at '%s'." % [
				level.name,
				pending_marker_path,
			],
		)
		return
	apply_marker(marker)


func apply_marker(marker: Node2D) -> void:
	(owner as Node2D).global_position = marker.global_position
	NetwEntity.of(owner).interpolation.reset()
	owner.reset_physics_interpolation()


@rpc("any_peer", "call_local", "reliable")
func commit_teleport() -> void:
	if multiplayer.get_remote_sender_id() != 1:
		return
	apply_pending_marker()
	Netw.sync_property(owner, &"position")
	finish_teleport.rpc()


@rpc("any_peer", "call_local", "reliable")
func finish_teleport() -> void:
	var authority := owner.get_multiplayer_authority()
	var sender := multiplayer.get_remote_sender_id()
	if sender != authority and multiplayer.get_unique_id() != authority:
		return
	NetwEntity.of(owner).interpolation.reset()
	owner.reset_physics_interpolation()
	pending_marker_path = ""
	if multiplayer.is_server():
		var engine := NetwEntity.of(owner).persistence
		if engine:
			engine.flush()
	if multiplayer.get_unique_id() == authority:
		await finish_local(OK, "")


@rpc("any_peer", "call_local", "reliable")
func fail_teleport(code: Error, detail: String) -> void:
	if multiplayer.get_remote_sender_id() != 1:
		return
	await finish_local(code, detail)


func finish_local(code: Error, detail: String) -> void:
	settle_until_msec = Time.get_ticks_msec() + int(settle_seconds * 1000.0)
	if code == OK:
		teleport_committed.emit()

	var layer := tp_layer()
	if layer:
		await layer.teleport_in()

	var peer_id := multiplayer.get_unique_id()
	var bucket := peer_bucket()
	var promise: NetwPromise = bucket.pending.get(peer_id) if bucket else null
	if promise:
		bucket.pending.erase(peer_id)
		if code == OK:
			promise.resolve(OK)
		else:
			promise.reject(code, detail)
	is_moving = false


func spawn(host: Node) -> void:
	ensure_current_scene_path()
	if current_scene_path.is_empty():
		push_error("Does not have a scene to tp into.")
		return
	var scene: NetwSceneHandle = Netw.scene(host, current_scene_name)
	if scene and scene.is_declared:
		scene.add_player(NetwEntity.of(owner))
