class_name Vehicle
extends Node3D

const DISPLAY_SMOOTH := 0.05

# Nodes

@onready var sphere: RigidBody3D = $Sphere
@onready var raycast: RayCast3D = $Ground

# Vehicle elements

@onready var vehicle_model = $Container
@onready var vehicle_body = get_node_or_null("Container/Model/body")

# (Optional) wheels

@onready var wheel_fl = get_node_or_null("Container/Model/wheel-front-left")
@onready var wheel_fr = get_node_or_null("Container/Model/wheel-front-right")
@onready var wheel_bl = get_node_or_null("Container/Model/wheel-back-left")
@onready var wheel_br = get_node_or_null("Container/Model/wheel-back-right")

# Effects

@onready var trail_left = get_node_or_null("Container/TrailLeft")
@onready var trail_right = get_node_or_null("Container/TrailRight")

# Sounds

@onready var screech_sound: AudioStreamPlayer3D = $Container/ScreechSound
@onready var engine_sound: AudioStreamPlayer3D = $Container/EngineSound
@onready var impact_sound: AudioStreamPlayer3D = $Container/ImpactSound

# Networking

@onready var entity := NetwEntity.of(self)

var pressed := {
	&"left": false,
	&"right": false,
	&"forward": false,
	&"back": false,
}

var input: Vector3
var normal: Vector3

var acceleration: float
var angular_speed: float
var linear_speed: float

var colliding: bool

var display_position: Vector3
var display_heading: float

var linear_velocity: Vector3
var prev_position: Vector3

var calculated_lean: float

var sphere_position: Vector3:
	get:
		return sphere.position if sphere else $Sphere.position
	set(value):
		if sphere:
			sphere.position = value

var sphere_linear_velocity: Vector3:
	get:
		return sphere.linear_velocity if sphere else $Sphere.linear_velocity
	set(value):
		if sphere:
			sphere.linear_velocity = value

var heading: float:
	get:
		return vehicle_model.rotation.y if vehicle_model \
		else $Container.rotation.y
	set(value):
		if vehicle_model:
			vehicle_model.rotation.y = value


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	e.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN

	Netw.configure_property(self, &"sphere_position").broadcast().masked() \
			.quantize(NetwQuantizeScalar.new().bits(19).limits(-2048.0, 2048.0)) \
			.on_spawn() \
			.interpolate(
				NetwInterpolate.new().lerp().smooth(DISPLAY_SMOOTH) \
						.project_by(&"sphere_linear_velocity") \
						.to(&"display_position"),
			)
	Netw.configure_property(self, &"sphere_linear_velocity") \
			.broadcast().masked() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-256.0, 256.0))
	Netw.configure_property(self, &"heading").broadcast().masked() \
			.quantize(NetwQuantizeAngle.new().bits(16).centered()) \
			.on_spawn() \
			.interpolate(
				NetwInterpolate.new().angle().smooth(DISPLAY_SMOOTH) \
						.project_by(&"angular_speed").to(&"display_heading"),
			)
	Netw.configure_property(self, &"linear_speed").broadcast().masked() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-4.0, 4.0))
	Netw.configure_property(self, &"angular_speed").broadcast().masked() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-16.0, 16.0))
	Netw.configure_property(self, &"acceleration").broadcast().masked()

	Netw.configure_interest(self).join(&"race")


func _ready() -> void:
	sphere.freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
	sphere.freeze = not entity.is_controlled_locally
	display_position = sphere.position
	display_heading = vehicle_model.rotation.y
	prev_position = vehicle_model.position


func _unhandled_input(event: InputEvent) -> void:
	if not entity.is_controlled_locally:
		return
	for action: StringName in pressed:
		if event.is_action(action):
			pressed[action] = event.is_action_pressed(action, true)

# Public Functions


func get_vehicle_position() -> Vector3:
	return vehicle_model.global_position


func displayed_position() -> Vector3:
	return get_vehicle_position()

# Functions


func _physics_process(delta):
	if not entity.is_controlled_locally:
		return

	handle_input(delta)

	var direction = sign(linear_speed)
	if direction == 0:
		direction = sign(input.z) if abs(input.z) > 0.1 else 1

	var steering_grip = clamp(abs(linear_speed), 0.2, 1.0)

	var target_angular = -input.x * steering_grip * 4 * direction
	angular_speed = lerp(angular_speed, target_angular, delta * 4)

	vehicle_model.rotate_y(angular_speed * delta)

	# Ground alignment

	if raycast.is_colliding():
		if !colliding:
			if vehicle_body != null:
				vehicle_body.position = Vector3(0, 0.1, 0) # Bounce
			input.z = 0

		normal = raycast.get_collision_normal()

		# Orient model to colliding normal

		if normal.dot(vehicle_model.global_basis.y) > 0.5:
			var saved_heading := heading
			var xform = align_with_y(vehicle_model.global_transform, normal)
			vehicle_model.global_transform = vehicle_model.global_transform \
					.interpolate_with(xform, 0.2).orthonormalized()
			heading = saved_heading

	colliding = raycast.is_colliding()

	var target_speed = input.z

	if (target_speed < 0 and linear_speed > 0.01):
		linear_speed = lerp(linear_speed, 0.0, delta * 8)
	else:
		if (target_speed < 0):
			linear_speed = lerp(linear_speed, target_speed / 2, delta * 2)
		else:
			linear_speed = lerp(linear_speed, target_speed, delta * 6)

	acceleration = lerpf(acceleration, linear_speed + (abs(sphere.angular_velocity.length() * linear_speed) / 100), delta * 1)

# Handle input when vehicle is colliding with ground


func handle_input(delta):
	if raycast.is_colliding():
		input.x = float(pressed[&"right"]) - float(pressed[&"left"])
		input.z = float(pressed[&"forward"]) - float(pressed[&"back"])

	sphere.angular_velocity += vehicle_model.get_global_transform().basis.x * (linear_speed * 100) * delta


func _process(delta):
	if entity.is_controlled_locally:
		# Match vehicle model to physics sphere

		vehicle_model.position = sphere.position - Vector3(0, 0.65, 0)
	else:
		vehicle_model.position = display_position - Vector3(0, 0.65, 0)
		vehicle_model.rotation.y = display_heading

	raycast.position = sphere.position

	# Calculate vehicle model linear velocity

	linear_velocity = (vehicle_model.position - prev_position) / maxf(delta, 0.0001)
	prev_position = vehicle_model.position

	# Visual and audio effects

	effect_engine(delta)
	effect_body(delta)
	effect_wheels(delta)
	effect_trails()


func effect_body(delta):
	calculated_lean = lerp_angle(calculated_lean, angular_speed / 20.0, delta * 5)

	# Slightly tilt (and move) body based on acceleration and steering

	if vehicle_body != null:
		vehicle_body.rotation.x = lerp_angle(vehicle_body.rotation.x, -(linear_speed - acceleration) / 6, delta * 10)
		vehicle_body.rotation.z = calculated_lean

		vehicle_body.position = vehicle_body.position.lerp(Vector3(0, 0.2, 0), delta * 5)


func effect_wheels(delta):
	# Rotate wheels based on acceleration

	for wheel in [wheel_fl, wheel_fr, wheel_bl, wheel_br]:
		if wheel != null:
			wheel.rotation.x += acceleration

	# Rotate front wheels based on steering direction

	if wheel_fl != null:
		wheel_fl.rotation.y = lerp_angle(
			wheel_fl.rotation.y,
			-input.x / 1.5,
			delta * 10,
		)
	if wheel_fr != null:
		wheel_fr.rotation.y = lerp_angle(
			wheel_fr.rotation.y,
			-input.x / 1.5,
			delta * 10,
		)

# Engine sounds


func effect_engine(delta):
	var speed_factor = clamp(abs(linear_speed), 0.0, 1.0)
	var throttle_factor = clamp(abs(input.z), 0.0, 1.0)

	var target_volume = remap(speed_factor + (throttle_factor * 0.5), 0.0, 1.5, -15.0, -5.0)
	engine_sound.volume_db = lerp(engine_sound.volume_db, target_volume, delta * 5.0)

	var target_pitch = remap(speed_factor, 0.0, 1.0, 0.5, 3)
	if throttle_factor > 0.1:
		target_pitch += 0.2

	engine_sound.pitch_scale = lerp(engine_sound.pitch_scale, target_pitch, delta * 2.0)

# Show trails (and play skid sound)


func effect_trails():
	var drift_intensity = abs(linear_speed - acceleration) + (abs(calculated_lean) * 2.0)
	var should_emit = drift_intensity > 0.25

	if trail_left != null:
		trail_left.emitting = should_emit
	if trail_right != null:
		trail_right.emitting = should_emit

	var target_volume = -80.0
	if should_emit:
		target_volume = remap(
			clamp(drift_intensity, 0.25, 2.0),
			0.25,
			2.0,
			-10.0,
			0.0,
		)

	screech_sound.pitch_scale = lerp(screech_sound.pitch_scale, clamp(abs(linear_speed), 1.0, 3.0), 0.1)
	screech_sound.volume_db = lerp(screech_sound.volume_db, target_volume, 10.0 * get_physics_process_delta_time())

# Align vehicle with normal


func align_with_y(xform, new_y):
	xform.basis.y = new_y
	xform.basis.x = -xform.basis.z.cross(new_y)
	xform.basis = xform.basis.orthonormalized()
	return xform

# Detect collisions and play impact sound


func _on_sphere_body_entered(_body: Node) -> void:
	if vehicle_body == null:
		return

	if not impact_sound.playing:
		var impact_velocity := absf(linear_velocity.dot(vehicle_body.global_basis.z))
		impact_sound.volume_db = clampf(remap(impact_velocity, 0.0, 6.0, -20.0, 0.0), -20.0, 0.0)
		impact_sound.play()
