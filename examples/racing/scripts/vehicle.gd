class_name Vehicle
extends Node3D
## A server-authoritative racing car with client-side prediction.
##
## The car is the stock ball controller: an invisible [RigidBody3D] sphere is
## the only dynamic body, propelled by pushing its angular velocity so Jolt's
## friction solve rolls it forward, while the visual Container integrates its
## own yaw and rides the sphere. The whole car is one networked entity whose
## state set lives on this root through accessor properties routing to the child
## sphere, so a single atomic snapshot carries the dynamic-body pose. Because the
## sphere's solver cannot be stepped per input, a recovery lands the
## authoritative pose on the solver in one write, projected to the present tick
## through each pose field's [method NetwInterpolate.project_by] derivative, and
## pauses around wall contacts. The velocity fields are teleport-only restores,
## so an in-domain sub-teleport recovery keeps momentum. An out-of-domain
## recovery restores the whole closure because its contact antecedents were not
## reproducible.
##
## Display is separated from simulation. On a remote peer the interpolated
## channels write dedicated display_position and display_heading targets, never
## the simulated sphere or heading, so the smoothed display value never re-enters
## the control loop. On a simulating peer the visual rides the live body plus a
## decaying correction offset seeded from
## [signal NetwLagCompensationInterface.PredictionHandle.recovered], so the body
## lands on truth while the rendered car glides onto it.

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle

# Remote-display interpolation blend (seconds).
const DISPLAY_SMOOTH := 0.05

# Chase smoothing for the simulating car's own visual: near-exact tracking, so
# the display never trails the body it predicts. Recovery absorption rides the
# interpolator's chase glide, not this.
const OWN_CHASE_SMOOTH := 0.005

# Capture-only switch that reproduces the pre-L1 schedule and drive feed.
const LEGACY_CAPTURE_VAR := "NETW_RACING_LEGACY_MODEL"

# Capture-only switch that arms the interpolation pump's per-frame trace.
const INTERP_TRACE_VAR := "NETW_RACING_INTERP_TRACE"

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

# Display targets the interpolator writes, kept out of the simulation. A remote
# car renders them from the buffered stream; a simulating car renders them from
# the predicted chase, which tracks the live body and absorbs each recovery as
# a decaying render offset, so the visual glides onto a corrected body instead
# of jumping with it. Position rides the Container origin (the tick never reads
# it); the chase's yaw offset rides the Model child, never the Container
# rotation the tick integrates and propels along.
var display_position: Vector3
var display_heading: float

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
	get:
		return sphere.position if is_instance_valid(sphere) else Vector3.ZERO
	set(value):
		_apply_sphere_position(value)

var sphere_linear_velocity: Vector3:
	get:
		return sphere.linear_velocity if is_instance_valid(sphere) else Vector3.ZERO
	set(value):
		if is_instance_valid(sphere):
			sphere.linear_velocity = value

var sphere_angular_velocity: Vector3:
	get:
		return sphere.angular_velocity if is_instance_valid(sphere) else Vector3.ZERO
	set(value):
		if is_instance_valid(sphere):
			sphere.angular_velocity = value

var heading: float:
	get:
		return vehicle_model.rotation.y if is_instance_valid(vehicle_model) else 0.0
	set(value):
		if is_instance_valid(vehicle_model):
			vehicle_model.rotation.y = value


func _init() -> void:
	var e := NetwEntity.resolve(self)
	e.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
	e.on_controller_disconnect = NetwEntity.DisconnectRule.DESPAWN

	# Solver-owned half: the sphere pose replicates atomically with its velocities.
	# The interpolated pose is redirected to display_position so the smoothed value
	# never writes back onto the simulated body, and it projects along the
	# replicated linear velocity so a remote car covers a gap without stalling.
	Netw.configure_property(self, &"sphere_position").state().masked().causal() \
			.quantize(NetwQuantizeBits.new().bits(24).limits(-2048.0, 2048.0)) \
			.on_spawn().epsilon(0.35) \
			.interpolate(
				NetwInterpolate.new().lerp().smooth(DISPLAY_SMOOTH) \
						.project_by(&"sphere_linear_velocity").to(&"display_position"),
			)
	Netw.configure_property(self, &"sphere_linear_velocity").state().masked() \
			.causal().teleport_only().epsilon(0.5) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-256.0, 256.0))
	Netw.configure_property(self, &"sphere_angular_velocity").state().masked() \
			.causal().teleport_only().epsilon(0.35) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-256.0, 256.0))

	# Heading interpolates as an angle channel onto its own display target, and
	# projects by its replicated angular_speed, its exact derivative, so a
	# correction restores it advanced to the present tick and blended by the
	# shortest arc instead of rewinding the turn in progress. The scalar
	# cosmetics replicate raw: a remote reads them straight for effects, so
	# they never need a smoothed display copy.
	# The epsilon is in radians, the only scale this field has: the entity
	# default is metres and decides nothing about an angle. Heading is also the
	# one causal field here that integrates with no rate answering for it, so
	# leaving it unscaled kept the direction the drive pushes along out of the
	# convergence measure entirely, free to accumulate until the propulsion
	# turned it into a momentum fork that some other field crossed for.
	Netw.configure_property(self, &"heading").state().masked().causal() \
			.reconcile_only().epsilon(0.05) \
			.quantize(NetwQuantizeAngle.new().bits(16).centered()) \
			.on_spawn() \
			.interpolate(
				NetwInterpolate.new().angle().smooth(DISPLAY_SMOOTH) \
						.project_by(&"angular_speed").to(&"display_heading"),
			)
	# linear_speed and angular_speed are first-order filters closing on a target
	# both peers compute from the same command, so a divergence between them
	# shrinks on its own inside the filter's own time constant. They are withheld
	# from a sub-teleport restore for the same reason the solver velocities are:
	# a recovery stages authority's value at the acknowledged transition, which is
	# older than the one they already hold, and while the filter is still moving
	# that older value sits further from the truth than the error it would have
	# replaced. Restoring them below the teleport tier therefore writes a worse
	# number than leaving them alone, and above it the whole closure is restored
	# because a body that far out holds nothing worth keeping. acceleration and
	# colliding are recomputed each step, from linear_speed and from the raycast,
	# so restoring them writes values the next step overwrites. None of the
	# scalars triggers a correction on its own, so their drift never teleports
	# the pose.
	Netw.configure_property(self, &"linear_speed").state().masked().causal() \
			.reconcile_only().teleport_only().epsilon(0.1) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-4.0, 4.0))
	Netw.configure_property(self, &"angular_speed").state().masked().causal() \
			.reconcile_only().teleport_only().epsilon(0.2) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-16.0, 16.0))
	# The steering-sign latch is state the next step reads, so a recovery must
	# restore it and a replay must not re-derive it from a diverging speed. It
	# only ever holds -1, 0 or +1, which is exactly the three levels two bits
	# spans, so quantizing it is lossless and gives the field a canonical form
	# the fingerprint can compare instead of raw float bits.
	Netw.configure_property(self, &"steer_direction").state().masked().causal() \
			.epsilon(0.0) \
			.quantize(NetwQuantizeBits.new().bits(2).limits(-1.0, 1.0))
	Netw.configure_property(self, &"acceleration").state().masked().derived() \
			.reconcile_only()
	Netw.configure_property(self, &"colliding").state().masked().derived() \
			.reconcile_only()

	Netw.configure_interest(self).layer(&"race")


func _ready() -> void:
	sphere.freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
	# A dynamic body against walls is the case the reference stance freezes rather
	# than extrapolates: a forecast tail projects a remote car straight into a wall
	# it has no geometry for, so remotes BUFFER and the small buffer covers the gap.
	var handle := entity.interpolation if entity else null
	if handle:
		handle.timeline_mode = NetwInterpolationInterface.TimelineMode.BUFFERED
		# Corrections land on the solver in one write; the declared chase
		# absorbs each one as a decaying render offset, so the visual glides
		# onto truth instead of jumping, and tracks the live body near-exactly
		# between corrections.
		handle.predicted_mode = NetwInterpolationInterface.PredictedMode.CHASE
		handle.predicted_smooth_time = OWN_CHASE_SMOOTH
		# Arms the display pump's own per-frame trace, for diagnosing a visual
		# that sits away from the body it is supposed to follow.
		if not OS.get_environment(INTERP_TRACE_VAR).is_empty():
			handle.trace_interval = 20
	if entity:
		# The car's SOLVER_BODY archetype is declared on the scene's
		# PredictionComponent, which bundles the frame cadence, the
		# repeat-last hold, the rebase-recover policy, and the projected
		# restore. Only the sensors stay in code: a sampler is a Callable a
		# scene cannot carry. The ground ray is the one world fact the drive
		# reads, so it is declared, sampled once before each drive, folded
		# into the environment digest, and read back inside the drive, so a
		# divergence born of a different ground contact is charged to the
		# environment instead of staying unattributed.
		entity.prediction.sensors().sample(&"ground", _sample_ground)
		entity.prediction.witness().contacts(_sample_contacts)
		entity.prediction.transport().corridor(_transport_corridor_clear)
		# The island is declared once, on the track scene, and every car inherits
		# it. Declaring one here instead would opt this car out of the scene rule
		# rather than refine it, and the rule carries the opt-in promotion.
		# The legacy capture reproduces the pre-L1 per-tick schedule.
		if not OS.get_environment(LEGACY_CAPTURE_VAR).is_empty():
			entity.prediction.schedule().tick()
		_start_net_log()
	display_position = sphere.position
	display_heading = vehicle_model.rotation.y
	prev_position = vehicle_model.position
	_update_simulation_freeze()

# Public Functions


func get_vehicle_position() -> Vector3:
	return vehicle_model.global_position


## The displayed world position of the car, read by the local chase camera.
func displayed_position() -> Vector3:
	return get_vehicle_position()

# Functions


# Samples the ground contact at the pre-drive sphere position, forced so the
# result reflects this tick's position rather than trailing the last physics
# step. Both peers then read the same contact at drive 0, where one had settled
# a step and the other had not. Declared through sensors(), so the
# engine samples it before each drive and the drive reads it back.
func _sample_ground() -> Dictionary:
	raycast.position = sphere.position
	raycast.force_raycast_update()
	return {
		colliding = raycast.is_colliding(),
		collider = _ground_collider_identity(),
		normal = raycast.get_collision_normal() if raycast.is_colliding() \
				else Vector3.UP,
	}


func _ground_collider_identity() -> String:
	if not raycast.is_colliding():
		return ""
	var collider := raycast.get_collider() as Node
	return "path:%s" % collider.get_path() if collider else ""


# Samples the class-level facts Jolt realized after the drive. Continuous
# manifold values remain outside the equality witness.
func _sample_contacts() -> Dictionary:
	return {
		colliders = sphere.get_colliding_bodies(),
		sleeping = sphere.sleeping,
		body_mode = sphere.freeze_mode if sphere.freeze else -1,
		collision_layer = sphere.collision_layer,
		collision_mask = sphere.collision_mask,
	}


# Sweeps the live sphere through the proposed present-time translation.
func _transport_corridor_clear(
		current: Dictionary,
		proposed: Dictionary,
) -> bool:
	if not current.has(&"sphere_position") \
			or not proposed.has(&"sphere_position"):
		return false
	var parent := sphere.get_parent_node_3d()
	if not parent:
		return false
	var parameters := PhysicsTestMotionParameters3D.new()
	parameters.from = sphere.global_transform
	parameters.motion = parent.global_basis * (
		proposed[&"sphere_position"] - current[&"sphere_position"]
	)
	parameters.margin = 0.001
	return not PhysicsServer3D.body_test_motion(
		sphere.get_rid(),
		parameters,
		PhysicsTestMotionResult3D.new(),
	)


# The authoritative simulation step, run on the server, the owning client, and
# a remote peer while an island promotes this car to simulated fidelity.
func _network_tick(delta, _tick, _is_fresh):
	# The drive reads the declared ground sample rather than re-querying the
	# ray, so the transition runs against exactly the facts its environment
	# digest describes.
	var ground: Dictionary = entity.prediction.sensor(&"ground", {
		colliding = false,
		normal = Vector3.UP,
	})
	_handle_input(delta)

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

	if bool(ground[&"colliding"]):
		if !colliding:
			if vehicle_body != null:
				vehicle_body.position = Vector3(0, 0.1, 0) # Bounce

		normal = ground[&"normal"]

		# Orient model to colliding normal

		if normal.dot(vehicle_model.global_basis.y) > 0.5:
			var saved_heading := heading
			var xform = align_with_y(vehicle_model.global_transform, normal)
			vehicle_model.global_transform = vehicle_model.global_transform \
					.interpolate_with(xform, 0.2).orthonormalized()
			heading = saved_heading

	colliding = bool(ground[&"colliding"])

	var target_speed = input.z

	if (target_speed < 0 and linear_speed > 0.01):
		linear_speed = lerp(linear_speed, 0.0, delta * 8)
	else:
		if (target_speed < 0):
			linear_speed = lerp(linear_speed, target_speed / 2, delta * 2)
		else:
			linear_speed = lerp(linear_speed, target_speed, delta * 6)

	acceleration = lerpf(acceleration, linear_speed + (abs(sphere.angular_velocity.length() * linear_speed) / 100), delta * 1)

# Reads the tape input independently of the local RayCast update phase.


func _handle_input(delta):
	input.x = inputs.steer
	input.z = inputs.throttle

	sphere.angular_velocity += _propulsion_axis() * (linear_speed * 100) * delta


# Builds the rolling axis from replicated yaw and the declared ground sample.
# Model pitch and roll are presentation state and must never feed prediction.
func _propulsion_axis() -> Vector3:
	if not OS.get_environment(LEGACY_CAPTURE_VAR).is_empty():
		return vehicle_model.global_basis.x
	var ground: Dictionary = entity.prediction.sensor(&"ground", {
		colliding = false,
		normal = Vector3.UP,
	})
	var ground_normal := Vector3.UP
	if bool(ground[&"colliding"]):
		ground_normal = (ground[&"normal"] as Vector3).normalized()
	var heading_forward := Vector3.FORWARD.rotated(Vector3.UP, heading)
	var ground_forward := heading_forward.slide(ground_normal)
	if ground_forward.is_zero_approx():
		return Vector3.RIGHT.rotated(Vector3.UP, heading)
	return ground_forward.normalized().cross(ground_normal).normalized()


# Presentation runs on every peer. A simulating car (owner or server) renders the
# live body plus the decaying correction offsets, so a reconciliation lands on
# the solver in one write while the visual glides onto it. A proxy remote
# renders interpolated targets while its physics body stays frozen.
# These writes are display-only. Propulsion reads replicated heading and the
# local ground normal, while the yaw glide rides the Model child.
func _process(delta):
	if not is_instance_valid(sphere):
		return

	_update_simulation_freeze()

	if multiplayer.is_server():
		# Authority renders its live body without an interpolation playhead.
		vehicle_model.position = sphere.position - Vector3(0, 0.65, 0)
		vehicle_model.rotation.y = heading
		if model_visual != null:
			model_visual.rotation.y = 0.0
	elif _should_simulate():
		if is_instance_valid(entity):
			# Feed the predictor the body's sleep state so a settled car is not sprung.
			entity.prediction.sleeping = sphere.sleeping
		# The chase writes the display targets: the live body plus each
		# recovery's decaying render offset. Yaw stays sim-owned on the
		# Container, so only the chase's heading offset rides the Model child.
		vehicle_model.position = display_position - Vector3(0, 0.65, 0)
		if model_visual != null:
			model_visual.rotation.y = angle_difference(heading, display_heading)
	else:
		vehicle_model.position = display_position - Vector3(0, 0.65, 0)
		vehicle_model.rotation.y = display_heading
		if model_visual != null:
			model_visual.rotation.y = 0.0
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
	if not is_instance_valid(sphere):
		return
	var want_frozen := not _should_simulate()
	if sphere.freeze != want_frozen:
		sphere.freeze = want_frozen


func _should_simulate() -> bool:
	if not multiplayer:
		return true
	if multiplayer.is_server():
		return true
	if not is_instance_valid(entity):
		return false
	if not entity.prediction.is_registered():
		return entity.is_controlled_locally
	if entity.prediction.sim_mode \
			!= NetwLagCompensationInterface.PredictionHandle.SimMode.DISPLAY:
		return true
	return entity.prediction.input_source \
			== NetwLagCompensationInterface.PredictionHandle.InputSource.NONE \
			and entity.is_controlled_locally


# Arms the cadence recorder when the environment asks for it, so a live two-
# instance run leaves a CSV to correlate corrections against clock and consume
# events. Inert on a normal run.
func _start_net_log() -> void:
	if not RacingNetLog.armed():
		return
	var clock: NetwClockInterface = multiplayer.clock if multiplayer else null
	if not clock:
		return
	if sphere.get_script() == null:
		sphere.set_script(VehicleContactProbe)
		sphere.max_contacts_reported = maxi(sphere.max_contacts_reported, 16)
	_net_log = RacingNetLog.new()
	_net_log.name = "NetLog"
	add_child(_net_log)
	_net_log.start(entity, clock)


# The sphere_position setter. Only a reconciliation correction reaches it now: the
# interpolator writes display_position, not the body, so there is no every-frame
# display write to fight Jolt and no display band to guard. A recovery lands its
# one staged write straight on the solver body.
func _apply_sphere_position(local_pos: Vector3) -> void:
	if not is_instance_valid(sphere):
		return
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


func _on_sphere_body_entered(body: Node) -> void:
	# Pause reconciliation through the contact: the predicted and authoritative
	# bodies settle a wall bounce differently for a few ticks, and correcting there
	# fights the solver. A hard desync past the teleport threshold still snaps.
	if _should_simulate() and is_instance_valid(entity):
		entity.prediction.notify_contact()
		if _net_log:
			_net_log.mark_contact(
				StringName("%s_entered" % body.get_class().to_snake_case()),
			)

	if vehicle_body == null:
		return

	if not impact_sound.playing:
		var impact_velocity := absf(linear_velocity.dot(vehicle_body.global_basis.z))
		impact_sound.volume_db = clampf(remap(impact_velocity, 0.0, 6.0, -20.0, 0.0), -20.0, 0.0)
		impact_sound.play()
