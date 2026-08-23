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
## authoritative pose on the solver in one write, advanced to the present tick
## through each pose field's
## [method NetwScriptModel.PropertyConfig.carry_along] channel, and pauses
## around wall contacts. The velocity fields are teleport-only restores, so an
## in-domain sub-teleport recovery keeps momentum. An out-of-domain recovery
## restores the whole closure because its contact antecedents were not
## reproducible.
##
## The two velocity fields declare no carry channel, so the wiring report names
## them: they can trigger a recovery that no sub-teleport restore may write, and
## the promoted closure that does write them lands the acknowledged value.
## [member NetwPredictionHandle.field_recovery] counts
## what that costs.
##
## Display is separated from simulation. On a remote peer the interpolated
## channels write dedicated display_position and display_heading targets, never
## the simulated sphere or heading, so the smoothed display value never re-enters
## the control loop. On a simulating peer the visual rides the live body plus a
## decaying correction offset seeded from
## [signal NetwPredictionHandle.recovered], so the body
## lands on truth while the rendered car glides onto it.


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

# Diagnostic-only switch that takes the recovery ladder out of the loop, so a
# divergence has to explain itself with nothing writing to the body.
#
# It answers one question: whether sustained contact against static geometry
# forks the two simulations on its own, or only does so once corrections are
# feeding back into it. Under
# [constant NetwPredict.RecoveryPolicy.OBSERVE] a divergence is still
# reported and still attributed, and nothing is ever restored, so the owner
# drifts from authority for the whole run. That drift is the price of the
# measurement and is not itself a finding: read the RATE the divergence grows
# at while resting on a wall against the rate while rolling, never the level.
# TODO: delete this once the resting-contact question is answered either way.
const OBSERVE_VAR := "NETW_RACING_OBSERVE"

# Measurement-only switch that declares the forward model for the sphere's spin:
# angular_propulsion is replicated and sphere_angular_velocity carries along it,
# so a recovery advances the acknowledged spin by the drive term over the
# unacknowledged span instead of writing it verbatim.
#
# This is Phase 1's carry_along arm, rebuilt. Phase 1 measured that it does NOT
# repair the momentum fork -- the solver cancels 80% of the term the game
# contributes -- so nothing here is a candidate default. It is kept because it is
# the only configuration that exercises K8: declaring the channel enrols a rad/s
# field in the teleport tier, and with one scalar threshold meaning metres that
# tripled teleports (6.3 -> 19.5 per run) on FEWER triggers. It is the arm the
# per-field teleport_at() fix has to be measured on.
# TODO: delete this once S4's measurement is recorded either way.
const CARRY_ANGVEL_VAR := "NETW_RACING_CARRY_ANGVEL"

# Measurement-only switch, read only while the arm above is armed: the tier
# distance sphere_angular_velocity declares for itself, in rad/s. Empty leaves it
# inheriting the entity's 3.0, which is the number that means metres.
#
# 8.0 is the value the measurement uses, and it is measured rather than picked:
# over 8645 POSE rows of the S2 gate set this sphere's own spin runs a median of
# 8.0 rad/s (p90 18.1, max 31.5), so an 8.0 rad/s error is one whose size is the
# whole rotation -- the point past which the predicted spin bears no relation to
# authority's and there is nothing worth keeping. The inherited 3.0 sits inside
# ordinary operation instead: 1.95% of measured comparisons reach it against
# 0.54% at 8.0.
const ANGVEL_TELEPORT_AT_VAR := "NETW_RACING_ANGVEL_TELEPORT_AT"

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

# The angular acceleration the drive applies this transition, in rad/s^2: the
# exact term _handle_input adds to the sphere's spin, published so a recovery can
# advance an acknowledged spin across the unacknowledged span rather than writing
# it verbatim. Recomputed every step from the replicated speed and heading and the
# declared ground sample, so it is derived state and never triggers a correction.
#
# Replicated rather than re-derived on the receiving peer, because the ground
# normal it is built from is a sensor sample rather than a synchronized field: a
# remote cannot reconstruct authority's axis, only its own.
var angular_propulsion: Vector3 = Vector3.ZERO


func _init() -> void:
	var e := NetwEntity.resolve(self)
	e.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	e.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN

	# Solver-owned half: the sphere pose replicates atomically with its velocities.
	# The interpolated pose is redirected to display_position so the smoothed value
	# never writes back onto the simulated body, and it projects along the
	# replicated linear velocity so a remote car covers a gap without stalling.
	# The position grid is sized against the divergence this car's own solver
	# produces, not against how finely a float can be described. Canonical form
	# is the quantized code, so two peers agree bit-for-bit only when they land
	# in the same bucket, and a grid finer than the divergence makes agreement
	# unreachable by construction however correct the codec is. Measured
	# contact-free over 1617 acknowledged transitions, the two peers fork by
	# about 74 um per axis, so this step is roughly fifty times the fork it has
	# to span. It stays far under anything that reads the value: 7.8 mm is about
	# 5% of how far the car travels in one frame, and the epsilon that triggers
	# a correction is 45 times larger.
	#
	# The velocity grids below are deliberately NOT coarsened to match. They
	# disagree on 2.5% and 1.0% of transitions, which is near the noise floor,
	# and their step is also the smallest divergence they can report, so
	# widening them would make every velocity disagreement louder to buy back
	# almost nothing.
	Netw.configure_property(self, &"sphere_position").state().masked().causal() \
			.quantize(NetwQuantizeBits.new().bits(19).limits(-2048.0, 2048.0)) \
			.on_spawn().epsilon(0.35) \
			.carry_along(&"sphere_linear_velocity") \
			.interpolate(
				NetwInterpolate.new().lerp().smooth(DISPLAY_SMOOTH) \
						.project_by(&"sphere_linear_velocity").to(&"display_position"),
			)
	Netw.configure_property(self, &"sphere_linear_velocity").state().masked() \
			.causal().teleport_only().epsilon(0.5) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-256.0, 256.0))
	var angular_velocity_config := Netw.configure_property(
		self,
		&"sphere_angular_velocity",
	).state().masked() \
			.causal().teleport_only().epsilon(0.35) \
			.quantize(NetwQuantizeBits.new().bits(16).limits(-256.0, 256.0))
	# The measurement arm, off by default and byte-identical to the line above
	# when it is off: no extra field on the wire, no channel, no enrolment.
	if not OS.get_environment(CARRY_ANGVEL_VAR).is_empty():
		angular_velocity_config.carry_along(&"angular_propulsion")
		var tier := OS.get_environment(ANGVEL_TELEPORT_AT_VAR)
		if not tier.is_empty():
			angular_velocity_config.teleport_at(float(tier))
		# derived() is what keeps publishing the drive term from demanding
		# recoveries of its own. Without it the field inherits the entity's 0.35
		# tolerance -- which means METRES -- and trips on every rad/s^2
		# disagreement. Measured when the correction decision still read only the
		# reconcile_only mark: 99 of 111 probation re-quarantines in one run had
		# angular_propulsion as their worst field, and the arm read as 32% of
		# frames demoted. reconcile_only() is kept alongside it because it states
		# the same intent at the field level, and racing's other derived fields
		# declare both marks for the same reason.
		Netw.configure_property(self, &"angular_propulsion").state().masked() \
				.derived().reconcile_only() \
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
			.carry_along(&"angular_speed") \
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
	var handle: NetwDisplayHandle = entity.interpolation if entity else null
	if handle:
		handle.timeline_mode = NetwDisplayHandle.TimelineMode.BUFFERED
		# Corrections land on the solver in one write; the declared chase
		# absorbs each one as a decaying render offset, so the visual glides
		# onto truth instead of jumping, and tracks the live body near-exactly
		# between corrections.
		handle.predicted_mode = NetwDisplayHandle.PredictedMode.CHASE
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
		# The breach response is declared here rather than inherited from the
		# archetype, because no archetype sets it any more: it is the one recovery
		# fact that changes sim_mode, so it arrives only when a game names it. This
		# car wants DEMOTE -- a witnessed wall contact outside the island is
		# exactly the case its speculation cannot reproduce -- and saying so keeps
		# the behaviour every capture in this campaign was measured under.
		entity.prediction.breach_response = NetwPredict.BreachResponse.DEMOTE
		entity.prediction.sensors[&"ground"] = _sample_ground
		entity.prediction.witness_contacts = _sample_contacts
		entity.prediction.transport_corridor = _transport_corridor_clear
		# The island is declared once, on the track scene, and every car inherits
		# it. Declaring one here instead would opt this car out of the scene rule
		# rather than refine it, and the rule carries the opt-in promotion.
		# The legacy capture reproduces the pre-L1 per-tick schedule.
		if not OS.get_environment(LEGACY_CAPTURE_VAR).is_empty():
			entity.prediction.schedule = NetwPredict.Schedule.TICK
		if not OS.get_environment(OBSERVE_VAR).is_empty():
			entity.prediction.recovery_policy = \
					NetwPredict.RecoveryPolicy.OBSERVE
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
# a step and the other had not. Declared through prediction.sensors, so the
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

	# Named before it is applied, in exactly the order it was applied in, so the
	# published rate is the term this transition used rather than a restatement of
	# it. The multiply is the same one, left to right, so this is bit-identical to
	# the single expression it replaced.
	angular_propulsion = _propulsion_axis() * (linear_speed * 100)
	sphere.angular_velocity += angular_propulsion * delta


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
			!= NetwPredict.SimMode.DISPLAY:
		return true
	return entity.prediction.input_source \
			== NetwPredict.InputSource.NONE \
			and entity.is_controlled_locally


# Arms the cadence recorder when the environment asks for it, so a live two-
# instance run leaves a CSV to correlate corrections against clock and consume
# events. Inert on a normal run.
func _start_net_log() -> void:
	if not RacingNetLog.armed():
		return
	var clock: NetwClockHandle = multiplayer._native_core.clock_handle if multiplayer else null
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
