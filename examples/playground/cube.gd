class_name PlayCube
extends RigidBody3D

const LIMIT := 64.0
const TINT_RATE := 10.0

var entity: NetwEntity
var tint := Color()
var painted: StandardMaterial3D

@onready var visual: MeshInstance3D = $Visual


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.transfer = NetwEntity.TRANSFER_IMMEDIATE
	entity.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	entity.simulation.claim_on_contact = true
	entity.simulation.release_on_rest = 1.0
	entity.interpolation.visual_root = ^"Visual"
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


func _ready() -> void:
	set_process(false)


func _process(delta: float) -> void:
	painted.albedo_color = painted.albedo_color.lerp(
		tint,
		1.0 - exp(-TINT_RATE * delta),
	)
	if painted.albedo_color.is_equal_approx(tint):
		painted.albedo_color = tint
		set_process(false)


func paint(color: Color) -> void:
	tint = color
	if painted == null:
		var shown := (visual.material_override as StandardMaterial3D).albedo_color
		if shown.is_equal_approx(color):
			return
		painted = StandardMaterial3D.new()
		painted.albedo_color = shown
		visual.material_override = painted
	set_process(true)


func claim(hold: NetwEntity.Hold) -> void:
	if entity.is_control_pending or entity.hold >= hold and (
		entity.is_controlled_locally or entity.controller != 0
	):
		return
	entity.claim_authority(hold).catch_error(refused)


func let_go() -> void:
	entity.claim_authority(NetwEntity.HOLD_YIELDABLE).catch_error(refused)


func refused(_code: Error, _detail: String) -> void:
	pass
