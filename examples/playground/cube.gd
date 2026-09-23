class_name PlayCube
extends RigidBody3D

const LIMIT := 64.0

var held_by := 0:
	set(value):
		held_by = value
		pin(value != 0)
var hold_offset := Transform3D.IDENTITY
var hand: Node3D

var entity: NetwEntity


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.transfer = NetwEntity.TRANSFER_IMMEDIATE
	entity.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	entity.simulation.claim_on_contact = true
	entity.simulation.release_on_rest = 1.0
	entity.simulation.mode_changed.connect(on_mode_changed)
	entity.interpolation.visual_root = ^"Visual"
	Netw.configure_property(self, &"held_by").broadcast()
	Netw.configure_property(self, &"position").broadcast().heartbeat(60) \
			.quantize(NetwQuantizeScalar.new().bits(20).limits(-LIMIT, LIMIT)) \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"quaternion").broadcast() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"linear_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"angular_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"sleeping").broadcast()


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		entity.entity_id = StringName(name)


func grab(by: Node3D) -> bool:
	entity.request_control(NetwEntity.HOLD_EXCLUSIVE).catch_error(lost)
	if not entity.is_controlled_locally:
		return false
	hand = by
	hold_offset = hand.global_transform.affine_inverse() * global_transform
	held_by = multiplayer.get_unique_id()
	return true


func throw(velocity: Vector3) -> void:
	if hand == null:
		return
	hand = null
	held_by = 0
	linear_velocity = velocity
	entity.request_control(NetwEntity.HOLD_YIELDABLE).catch_error(lost)


func lost(_code: Error, _detail: String) -> void:
	hand = null


func on_mode_changed(
	_previous: NetwSimulationHandle.Mode, mode: NetwSimulationHandle.Mode
) -> void:
	if mode == NetwSimulationHandle.MODE_AUTHORITY and hand == null:
		held_by = 0


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	if hand != null:
		global_transform = hand.global_transform * hold_offset


func pin(frozen: bool) -> void:
	if freeze == frozen:
		return
	freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
	freeze = frozen
	if frozen and is_inside_tree():
		global_transform = global_transform
