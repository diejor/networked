class_name RocketCar
extends RigidBody3D

const TEAM_COLORS: Array[Color] = [Color.RED, Color.BLUE]
const ROOF_TORQUE := 800.0

@export var suspension_rest_dist := 0.5
@export var spring_strength := 100.0
@export var spring_damper := 10.0
@export var wheel_radius := 0.33
@export var engine_power := 200.0
@export var steering_angle := 25.0
@export var front_tire_grip := 1.5
@export var rear_tire_grip := 2.0
@export var jump_force := 30.0

var team := 0
var slot := 0

var accel_input := 0.0
var steering_input := 0.0
var spring_lengths: Array[float] = [0.0, 0.0, 0.0, 0.0]

var motion := Vector2.ZERO
var jumping := false
var ai_enabled := false
var ai_motion := Vector2.ZERO
var ai_jumping := false
var pressed := {
	&"left": false,
	&"right": false,
	&"forward": false,
	&"back": false,
	&"bounce": false,
}
@onready var car_model: Node3D = $Car_model
@onready var roof_bounce: RayCast3D = $RoofBounce
@onready var speed_label: Label3D = $Label3D
@onready var wheels: Array[RocketWheel] = [
	$Wheels/FL_Wheel,
	$Wheels/FR_Wheel,
	$Wheels/RL_Wheel,
	$Wheels/RR_Wheel,
]
@onready var entity := NetwEntity.of(self)
@onready var state := PhysicsServer3D.body_get_direct_state(get_rid())
@onready var level: Node = entity.scene.root
@onready var game: RocketGame = level.get_node(^"game|0")
@onready var marker: Marker3D = level.get_node(
	"Markers/%s%d" % ["r" if team == 0 else "b", slot + 1],
)

var pose: Transform3D:
	get:
		if state and not freeze:
			return state.transform
		return global_transform if is_inside_tree() else transform
	set(value):
		if state and not freeze:
			state.transform = value
		elif is_inside_tree():
			global_transform = value
		else:
			transform = value

var car_position: Vector3:
	get:
		return pose.origin
	set(value):
		var next := pose
		next.origin = value
		pose = next

var car_rotation: Quaternion:
	get:
		return pose.basis.get_rotation_quaternion()
	set(value):
		var next := pose
		next.basis = Basis(value.normalized())
		pose = next

var car_linear_velocity: Vector3:
	get:
		return state.linear_velocity
	set(value):
		state.linear_velocity = value

var car_angular_velocity: Vector3:
	get:
		return state.angular_velocity
	set(value):
		state.angular_velocity = value

var car_sleeping: bool:
	get:
		return state.sleeping
	set(value):
		state.sleeping = value

var spring_fl: float:
	get:
		return spring_lengths[0]
	set(value):
		spring_lengths[0] = value

var spring_fr: float:
	get:
		return spring_lengths[1]
	set(value):
		spring_lengths[1] = value

var spring_rl: float:
	get:
		return spring_lengths[2]
	set(value):
		spring_lengths[2] = value

var spring_rr: float:
	get:
		return spring_lengths[3]
	set(value):
		spring_lengths[3] = value

var steer_angle: float:
	get:
		return wheels[0].rotation.y
	set(value):
		wheels[0].rotation.y = value
		wheels[1].rotation.y = value


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	e.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN
	Netw.configure_property(self, &"motion").input().quantize(
		NetwQuantizeScalar.new().bits(8).limits(-1.0, 1.0),
	)
	Netw.configure_property(self, &"jumping").input()

	Netw.configure_property(self, &"car_position").state().masked().causal() \
			.on_spawn().teleport_at(1.5) \
			.quantize(NetwQuantizeScalar.new().bits(19).limits(-128.0, 128.0)) \
			.interpolate(
				NetwInterpolate.new().lerp().snap_at(3.0) \
						.project_by(&"car_linear_velocity").to(&"position"),
			)
	Netw.configure_property(self, &"car_rotation").state().masked().causal() \
			.on_spawn() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"car_linear_velocity").state().masked() \
			.causal().teleport_only() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-128.0, 128.0))
	Netw.configure_property(self, &"car_angular_velocity").state().masked() \
			.causal().teleport_only() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-64.0, 64.0))
	Netw.configure_property(self, &"car_sleeping").state().masked().causal()

	for spring: StringName in [
		&"spring_fl",
		&"spring_fr",
		&"spring_rl",
		&"spring_rr",
	]:
		Netw.configure_property(self, spring).state().masked().causal() \
				.reconcile_only() \
				.quantize(NetwQuantizeScalar.new().bits(12).limits(0.0, 1.0))
	Netw.configure_property(self, &"steer_angle").state().masked().causal() \
			.reconcile_only() \
			.quantize(NetwQuantizeAngle.new().bits(12).centered())

	Netw.configure_interest(self).join(&"arena")

	e.prediction.archetype = NetwPredict.ARCHETYPE_SOLVER_BODY
	e.prediction.schedule = RocketJoltStepper.schedule()
	e.prediction.recovery_policy = NetwPredict.RECOVERY_POLICY_REBASE_REPLAY
	e.prediction.witness_contacts = sample_contacts
	e.interpolation.visual_root = ^"Car_model"


func _ready() -> void:
	var clock: NetwClockHandle = Netw.clock(self)
	clock.before_tick.connect(gather_input)
	contact_monitor = true
	max_contacts_reported = 8

	set_color(TEAM_COLORS[team])

	if entity.is_controlled_locally:
		get_viewport().get_camera_3d().target = car_model


func _unhandled_input(event: InputEvent) -> void:
	if not entity.is_controlled_locally:
		return
	for action: StringName in pressed:
		if event.is_action(action):
			pressed[action] = event.is_action_pressed(action, true)


func gather_input(_delta: float, _tick: int) -> void:
	if not entity.is_controlled_locally:
		return
	motion = ai_motion if ai_enabled else Vector2(
		float(pressed[&"right"]) - float(pressed[&"left"]),
		float(pressed[&"forward"]) - float(pressed[&"back"]),
	).normalized()
	jumping = ai_jumping if ai_enabled else pressed[&"bounce"]


func _process(_delta: float) -> void:
	speed_label.text = "Speed: %.2f" % linear_velocity.length()


func _network_tick(delta: float, tick: int, _is_fresh: bool) -> void:
	if tick < game.kickoff_tick:
		take_kickoff_position()
		return

	accel_input = -clampf(motion.y, -1.0, 1.0)
	steering_input = -clampf(motion.x, -1.0, 1.0)

	var steering_rotation := steering_input * steering_angle
	if is_zero_approx(steering_rotation):
		steer_angle = lerpf(steer_angle, 0.0, 0.2)
	else:
		var angle := clampf(
			steer_angle + steering_rotation,
			-steering_angle,
			steering_angle,
		)
		steer_angle = lerpf(steer_angle, angle * delta, 0.3)

	var wheels_on_ground := 0
	for i in 4:
		var wheel: RocketWheel = wheels[i]
		wheel.previous_spring_length = spring_lengths[i]
		wheel.wheel_tick(delta)
		wheels_on_ground += int(wheel.is_colliding())
		spring_lengths[i] = wheel.previous_spring_length

	#Jump
	if wheels_on_ground > 2 and jumping:
		apply_impulse(jump_force * basis.y, -basis.y)

	#If fallen on roof, roll over
	roof_bounce.force_raycast_update()
	if roof_bounce.is_colliding() and wheels_on_ground == 0:
		apply_torque(basis.z * ROOF_TORQUE)


func take_kickoff_position() -> void:
	var origin := marker.global_position
	var facing := Vector3(0.0, origin.y, 0.0) - origin
	state.transform = Transform3D(
		Basis.looking_at(facing, Vector3.UP, true) if facing.length() > 0.001 \
		else Basis.IDENTITY,
		origin,
	)
	state.linear_velocity = Vector3.ZERO
	state.angular_velocity = Vector3.ZERO


func set_color(color: Color) -> void:
	var mesh := car_model.get_node(^"Car") as MeshInstance3D
	var painted := mesh.mesh.surface_get_material(0).duplicate() \
			as StandardMaterial3D
	painted.albedo_color = color
	mesh.set_surface_override_material(0, painted)


func sample_contacts() -> Dictionary:
	return {
		colliders = get_colliding_bodies(),
		sleeping = car_sleeping,
		collision_layer = collision_layer,
		collision_mask = collision_mask,
	}
