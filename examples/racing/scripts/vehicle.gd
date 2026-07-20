class_name Vehicle extends Node3D
## A server-authoritative racing car with client-side prediction.
##
## The car is the stock ball controller: an invisible [RigidBody3D] sphere is
## the only dynamic body, propelled by pushing its angular velocity so Jolt's
## friction solve rolls it forward, while the visual Container integrates its
## own yaw and rides the sphere. The whole car is one networked entity whose
## state set lives on this root through accessor properties routing to the child
## sphere, so a single atomic snapshot carries the dynamic-body pose. Because the
## sphere's solver cannot be stepped per input, prediction reconciles by
## SNAP_BLEND at full stiffness: a correction lands the authoritative pose on the
## solver in one write, projected to the present tick through each pose field's
## [method NetwInterpolate.project_by] derivative, and pauses around wall
## contacts. The contractive fields (velocities, speed scalars) are
## teleport-only restores, so a sub-threshold correction never rewinds momentum.
##
## Display is separated from simulation. On a remote peer the interpolated
## channels write dedicated display_position and display_heading targets, never
## the simulated sphere or heading, so the smoothed display value never re-enters
## the control loop. On a simulating peer the visual rides the live body plus a
## decaying correction offset seeded from
## [signal NetwLagCompensationInterface.PredictionHandle.pose_corrected], so the
## body lands on truth while the rendered car glides onto it.

# Remote-display interpolation blend (seconds).
const DISPLAY_SMOOTH := 0.05

# Time constant of the correction render offsets' exponential decay (seconds).
const CORRECTION_GLIDE := 0.15

# Nodes

@onready var sphere: RigidBody3D = $Sphere
@onready var raycast: RayCast3D = $Ground

# Vehicle elements

@onready var vehicle_model = $Container
@onready var model_visual = get_node_or_null("Container/Model")
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

@onready var inputs: Node = $Inputs
@onready var entity := NetwEntity.of(self)

var input: Vector3
var normal: Vector3

var acceleration: float
var angular_speed: float
var linear_speed: float

var colliding: bool

# Steering direction latched with hysteresis so client and server never fork the
# steering sign on an infinitesimal linear_speed difference near zero.
var steer_direction: float = 1.0

# Display targets the interpolator writes, kept out of the simulation. A remote car
# renders from these; a simulating car ignores them and renders the live body.
var display_position: Vector3
var display_heading: float

# Render offsets a correction seeds and _process decays, so the simulating car's
# visual glides onto the corrected body instead of jumping with it. Position
# rides the Container origin (the tick never reads it); yaw rides the Model
# child, never the Container rotation the tick integrates and propels along.
var correction_offset: Vector3
var correction_yaw: float

# Cadence recorder, present only when the environment arms it.
var _net_log: RacingNetLog = null

var linear_velocity: Vector3
var prev_position: Vector3

var calculated_lean: float


# Solver-owned half of the state set, routed to the child sphere. The position
# setter teleports through the PhysicsServer on a live body so a SNAP restore
# takes on a dynamic Jolt body, and moves the frozen kinematic body directly on
# a remote where the interpolator drives the pose.
var sphere_position: Vector3:
	get: return sphere.position if is_instance_valid(sphere) else Vector3.ZERO
	set(value): _apply_sphere_position(value)

var sphere_linear_velocity: Vector3:
	get: return sphere.linear_velocity if is_instance_valid(sphere) else Vector3.ZERO
	set(value):
		if is_instance_valid(sphere): sphere.linear_velocity = value

var sphere_angular_velocity: Vector3:
	get: return sphere.angular_velocity if is_instance_valid(sphere) else Vector3.ZERO
	set(value):
		if is_instance_valid(sphere): sphere.angular_velocity = value

var heading: float:
	get: return vehicle_model.rotation.y if is_instance_valid(vehicle_model) else 0.0
	set(value):
		if is_instance_valid(vehicle_model): vehicle_model.rotation.y = value


func _init() -> void:
	var e := NetwEntity.resolve(self)
	e.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
	e.on_controller_disconnect = NetwEntity.DisconnectRule.DESPAWN

	# Solver-owned half: the sphere pose replicates atomically with its velocities.
	# The interpolated pose is redirected to display_position so the smoothed value
	# never writes back onto the simulated body, and it projects along the
	# replicated linear velocity so a remote car covers a gap without stalling.
	Netw.configure_property(self, &"sphere_position").state().masked().on_spawn() \
			.interpolate(NetwInterpolate.new().lerp().smooth(DISPLAY_SMOOTH) \
			.project_by(&"sphere_linear_velocity").to(&"display_position"))
	Netw.configure_property(self, &"sphere_linear_velocity").state().masked()
	Netw.configure_property(self, &"sphere_angular_velocity").state().masked()

	# Heading interpolates as an angle channel onto its own display target, and
	# projects by its replicated angular_speed, its exact derivative, so a
	# correction restores it advanced to the present tick and blended by the
	# shortest arc instead of rewinding the turn in progress. The scalar
	# cosmetics replicate raw: a remote reads them straight for effects, so
	# they never need a smoothed display copy.
	Netw.configure_property(self, &"heading").state().masked().on_spawn() \
			.interpolate(NetwInterpolate.new().angle().smooth(DISPLAY_SMOOTH) \
			.project_by(&"angular_speed").to(&"display_heading"))
	Netw.configure_property(self, &"linear_speed").state().masked()
	Netw.configure_property(self, &"angular_speed").state().masked()
	Netw.configure_property(self, &"acceleration").state().masked()
	Netw.configure_property(self, &"colliding").state().masked()

	Netw.configure_interest(self).layer(&"race")


func _ready() -> void:
	sphere.freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
	# A dynamic body against walls is the case the reference stance freezes rather
	# than extrapolates: a forecast tail projects a remote car straight into a wall
	# it has no geometry for, so remotes BUFFER and the small buffer covers the gap.
	var handle := entity.interpolation if entity else null
	if handle:
		handle.timeline_mode = NetwInterpolationInterface.TimelineMode.BUFFERED
	# Corrections land on the solver in one write; the visual absorbs each one as
	# a decaying offset so the car glides onto truth instead of jumping.
	if entity:
		entity.prediction.pose_corrected.connect(_on_pose_corrected)
		_start_net_log()
	display_position = sphere.position
	display_heading = vehicle_model.rotation.y
	prev_position = vehicle_model.position
	_update_simulation_freeze()

# Public Functions

func get_vehicle_position() -> Vector3: return vehicle_model.global_position

## The displayed world position of the car, read by the local chase camera.
func displayed_position() -> Vector3: return get_vehicle_position()

# Functions

# The authoritative simulation step, run on the server and predicted on the
# owning client. A remote peer never runs it; the sphere is frozen and the
# interpolator drives the displayed pose.
func _network_tick(delta, _tick, _is_fresh):

	handle_input(delta)

	# Latch the steering direction with hysteresis. sign(linear_speed) flips on an
	# infinitesimal state difference near zero, so client and server would steer
	# opposite ways for a few ticks during a low-speed shuffle or a 180. The latch
	# only flips past a small band, keeping the steering sign in lockstep.
	if linear_speed > 0.05:
		steer_direction = 1.0
	elif linear_speed < -0.05:
		steer_direction = -1.0
	elif abs(input.z) > 0.1:
		steer_direction = sign(input.z)

	var steering_grip = clamp(abs(linear_speed), 0.2, 1.0)

	var target_angular = -input.x * steering_grip * 4 * steer_direction
	angular_speed = lerp(angular_speed, target_angular, delta * 4)

	vehicle_model.rotate_y(angular_speed * delta)

	# Ground alignment

	if raycast.is_colliding():
		if !colliding:
			if vehicle_body != null: vehicle_body.position = Vector3(0, 0.1, 0) # Bounce
			input.z = 0

		normal = raycast.get_collision_normal()

		# Orient model to colliding normal

		if normal.dot(vehicle_model.global_basis.y) > 0.5:
			var xform = align_with_y(vehicle_model.global_transform, normal)
			vehicle_model.global_transform = vehicle_model.global_transform.interpolate_with(xform, 0.2).orthonormalized()

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

	raycast.position = sphere.position

# Handle input when vehicle is colliding with ground

func handle_input(delta):

	if raycast.is_colliding():
		input.x = inputs.steer
		input.z = inputs.throttle

	sphere.angular_velocity += vehicle_model.get_global_transform().basis.x * (linear_speed * 100) * delta


# Presentation runs on every peer. A simulating car (owner or server) renders the
# live body plus the decaying correction offsets, so a reconciliation lands on
# the solver in one write while the visual glides onto it; a remote car renders
# the interpolated display targets and never touches its frozen physics body.
# These writes are display-only: the tick reads the Container basis for
# propulsion, never its origin, and the yaw glide rides the Model child.
func _process(delta):
	if not is_instance_valid(sphere): return

	_update_simulation_freeze()

	var decay := exp(-delta / CORRECTION_GLIDE)
	correction_offset *= decay
	correction_yaw *= decay

	if _should_simulate():
		if is_instance_valid(entity):
			# Feed the predictor the body's sleep state so a settled car is not sprung.
			entity.prediction.sleeping = sphere.sleeping
		vehicle_model.position = sphere.position + correction_offset - Vector3(0, 0.65, 0)
	else:
		vehicle_model.position = display_position - Vector3(0, 0.65, 0)
		vehicle_model.rotation.y = display_heading
	if model_visual != null:
		model_visual.rotation.y = correction_yaw
	raycast.position = sphere.position

	# Calculate vehicle model linear velocity

	linear_velocity = (vehicle_model.position - prev_position) / maxf(delta, 0.0001)
	prev_position = vehicle_model.position

	# Visual and audio effects

	effect_engine(delta)
	effect_body(delta)
	effect_wheels(delta)
	effect_trails()


# A remote-displayed car freezes its own sphere so Jolt never fights the
# interpolated pose. The framework's role-driven freeze only reaches an entity
# whose root is itself the body, so a child-body car manages freeze here.
func _update_simulation_freeze() -> void:
	if not is_instance_valid(sphere): return
	var want_frozen := not _should_simulate()
	if sphere.freeze != want_frozen:
		sphere.freeze = want_frozen


func _should_simulate() -> bool:
	if not multiplayer: return true
	if multiplayer.is_server(): return true
	return is_instance_valid(entity) and entity.is_controlled_locally


# Arms the cadence recorder when the environment asks for it, so a live two-
# instance run leaves a CSV to correlate corrections against clock and consume
# events. Inert on a normal run.
func _start_net_log() -> void:
	if not RacingNetLog.armed():
		return
	var clock: NetwClockInterface = multiplayer.clock if multiplayer else null
	if not clock:
		return
	_net_log = RacingNetLog.new()
	_net_log.name = "NetLog"
	add_child(_net_log)
	_net_log.start(entity, clock)


# Seeds the render offsets with the pose change a correction just applied, so the
# visual stays where it was and glides onto the corrected body. A teleport-tier
# correction clears them: a genuine desync should be seen to snap.
func _on_pose_corrected(deltas: Dictionary, teleported: bool) -> void:
	if teleported:
		correction_offset = Vector3.ZERO
		correction_yaw = 0.0
		return
	correction_offset -= deltas.get(&"sphere_position", Vector3.ZERO)
	correction_yaw -= deltas.get(&"heading", 0.0)


# The sphere_position setter. Only a reconciliation correction reaches it now: the
# interpolator writes display_position, not the body, so there is no every-frame
# display write to fight Jolt and no display band to guard. A full-stiffness
# SNAP_BLEND correction lands its one restore write straight on the solver body.
func _apply_sphere_position(local_pos: Vector3) -> void:
	if not is_instance_valid(sphere): return
	var target := global_transform * local_pos
	if sphere.freeze:
		sphere.global_position = target
		return
	var xform := sphere.global_transform
	xform.origin = target
	PhysicsServer3D.body_set_state(sphere.get_rid(), PhysicsServer3D.BODY_STATE_TRANSFORM, xform)


func effect_body(delta):

	calculated_lean = lerp_angle(calculated_lean, -input.x / 5 * linear_speed, delta * 5)

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

	if wheel_fl != null: wheel_fl.rotation.y = lerp_angle(wheel_fl.rotation.y, -input.x / 1.5, delta * 10)
	if wheel_fr != null: wheel_fr.rotation.y = lerp_angle(wheel_fr.rotation.y, -input.x / 1.5, delta * 10)

# Engine sounds

func effect_engine(delta):

	var speed_factor = clamp(abs(linear_speed), 0.0, 1.0)
	var throttle_factor = clamp(abs(input.z), 0.0, 1.0)

	var target_volume = remap(speed_factor + (throttle_factor * 0.5), 0.0, 1.5, -15.0, -5.0)
	engine_sound.volume_db = lerp(engine_sound.volume_db, target_volume, delta * 5.0)

	var target_pitch = remap(speed_factor, 0.0, 1.0, 0.5, 3)
	if throttle_factor > 0.1: target_pitch += 0.2

	engine_sound.pitch_scale = lerp(engine_sound.pitch_scale, target_pitch, delta * 2.0)

# Show trails (and play skid sound)

func effect_trails():

	var drift_intensity = abs(linear_speed - acceleration) + (abs(calculated_lean) * 2.0)
	var should_emit = drift_intensity > 0.25

	if trail_left != null: trail_left.emitting = should_emit
	if trail_right != null: trail_right.emitting = should_emit

	var target_volume = -80.0
	if should_emit: target_volume = remap(clamp(drift_intensity, 0.25, 2.0), 0.25, 2.0, -10.0, 0.0)

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

	# Pause reconciliation through the contact: the predicted and authoritative
	# bodies settle a wall bounce differently for a few ticks, and correcting there
	# fights the solver. A hard desync past the teleport threshold still snaps.
	if _should_simulate() and is_instance_valid(entity):
		entity.prediction.notify_contact()
		if _net_log:
			_net_log.mark_contact()

	if vehicle_body == null: return

	if not impact_sound.playing:
		var impact_velocity := absf(linear_velocity.dot(vehicle_body.global_basis.z))
		impact_sound.volume_db = clampf(remap(impact_velocity, 0.0, 6.0, -20.0, 0.0), -20.0, 0.0)
		impact_sound.play()
