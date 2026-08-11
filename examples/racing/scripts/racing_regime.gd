class_name RacingRegime
extends RefCounted
## Racing's adapter for [NetwRegimePeer], the scripted half of a two-process
## capture.
##
## The peer owns the lifecycle, the throttle, the watchdog, and the summary.
## This adapter contributes what only racing knows: how to host or join its
## session, when its car is ready, and how to drive. The gesture set covers
## the campaign's measured regimes: sustained cornering, the original felt
## symptom, and the contact probes.
## [codeblock]
## laps       throttle held, steer legs alternating, the lap stand-in
## symptom    accelerate and release cycles, then rotating in place
## slalom     three steer legs, the original scripted turn
## wall       full throttle into the outer wall, no steering
## wall_grind sustained wall contact with steer held into it
## wall_once  one grazing wall hit, then driving on with no further contact
## wall_hard  repeated square wall hits with reverse backoffs
## contact    closed-loop aim at the other car
## hold       no input for the whole horizon
## [/codeblock]
## The legacy [code]NETW_RACING_CAPTURE_*[/code] environment variables keep
## working as a fallback spelling of the same contract.

const LEGACY_ROLE_VAR := "NETW_RACING_CAPTURE_ROLE"
const LEGACY_PORT_VAR := "NETW_RACING_CAPTURE_PORT"
const LEGACY_SECONDS_VAR := "NETW_RACING_CAPTURE_SECONDS"
const LEGACY_MODE_VAR := "NETW_RACING_CAPTURE_MODE"

# Seconds one cornering leg holds its steer direction before reversing.
const LAPS_LEG_SECONDS := 3.0
# Seconds a wall grind drives straight before it expects to have arrived.
const WALL_GRIND_APPROACH_SECONDS := 8.0
# Physics frames out of contact before a grind reverses its steer to come back.
# Solver contact flickers frame to frame even while a car is pinned, so a
# reversal on the first empty frame would chatter instead of hold.
const WALL_GRIND_REACQUIRE_FRAMES := 10
# Seconds a wall_once gesture holds its graze before leaving. Long enough to
# carry angular content into the contact, short enough that the run is a HIT
# and the rest of it is the aftermath.
const WALL_ONCE_GRAZE_SECONDS := 2.0
# Seconds of reverse that separate the car from the wall it grazed, so the
# divergence outlives its cause instead of being continuously re-fed.
const WALL_ONCE_BACKOFF_SECONDS := 1.2
# Metres the aftermath keeps from the other car, in two bands. Sized from the
# track, not guessed: the two cars spawn 5.3 m apart and never get further than
# 19.4 m, and the two spheres touch at about 1.0 m centre to centre.
#
# Steering away alone was measured insufficient at 3.0 m -- the car turned and
# still closed to 1.00 m, because on a track this small a rolling sphere cannot
# out-steer its own momentum. So the outer band turns away and the inner band
# stops closing at all, by lifting the throttle and reversing.
const WALL_ONCE_CLEARANCE := 4.5
const WALL_ONCE_YIELD := 2.5
# The symptom gesture's cycle, matching the reported repro: accelerate one to
# two seconds, release, wait for the episode to close, repeat.
const SYMPTOM_DRIVE_SECONDS := 1.5
const SYMPTOM_RELEASE_SECONDS := 2.5
const SYMPTOM_ROTATE_SECONDS := 2.0

var _session: Node
var _api: NetwMultiplayer
var _travel := 0.0
# Wall-contact frames inside the wall_once graze, and inside everything after
# it. A total contact fraction cannot separate the two, and for this arm the
# split IS the regime: the hit has to land, and then it has to be over.
var _graze_ticks := -1
var _after_ticks := -1


## True when either the generic regime contract or the legacy racing capture
## variables name a role for this process.
static func armed() -> bool:
	return NetwRegimePeer.armed() \
			or not OS.get_environment(LEGACY_ROLE_VAR).is_empty()


## Builds, configures, and parents a [NetwRegimePeer] under [param session],
## with racing's gestures and session hooks installed. The caller starts it
## with [method NetwRegimePeer.run].
static func attach(session: Node, api: NetwMultiplayer) -> NetwRegimePeer:
	var adapter := RacingRegime.new()
	adapter._session = session
	adapter._api = api
	var peer := NetwRegimePeer.new()
	peer.api = api
	peer.connect_session = adapter._connect_session
	peer.await_local_ready = adapter._await_local_ready
	peer.summary_handles = adapter._summary_handles
	peer.condition_evidence = adapter._condition_evidence
	peer.gestures = {
		"laps": adapter._drive_laps,
		"symptom": adapter._drive_symptom,
		"slalom": adapter._drive_slalom,
		"wall": adapter._drive_wall,
		"wall_grind": adapter._drive_wall_grind,
		"wall_once": adapter._drive_wall_once,
		"wall_hard": adapter._drive_wall_hard,
		"contact": adapter._drive_at_other_car,
		"hold": adapter._hold,
	}
	session.add_child(peer)
	peer.configure_from_args()
	adapter._apply_legacy_env(peer)
	# The adapter must outlive the RefCounted scope it was built in, so the
	# peer's gesture callables keep it alive through their bound receiver.
	peer.set_meta(&"racing_adapter", adapter)
	return peer


# Maps the legacy env spelling onto the peer where the generic contract said
# nothing, so existing capture scripts keep working unchanged.
func _apply_legacy_env(peer: NetwRegimePeer) -> void:
	if NetwRegimePeer.armed():
		return
	peer.role = OS.get_environment(LEGACY_ROLE_VAR).to_lower()
	var legacy_port := OS.get_environment(LEGACY_PORT_VAR).to_int()
	if legacy_port > 0:
		peer.port = legacy_port
	var legacy_seconds := OS.get_environment(LEGACY_SECONDS_VAR).to_float()
	peer.seconds = legacy_seconds if legacy_seconds > 0.0 else 8.0
	if peer.role != NetwRegimePeer.ROLE_CLIENT:
		# The legacy host held still in every mode, so a contact probe had one
		# moving body and one stationary target.
		peer.gesture = "hold"
		return
	var mode := OS.get_environment(LEGACY_MODE_VAR).to_lower()
	peer.gesture = mode if peer.gestures.has(mode) else "slalom"


func _connect_session(role: String, port: int) -> Error:
	var payload := JoinPayload.new()
	payload.username = &"mario" if role == NetwRegimePeer.ROLE_HOST else &"luigi"
	if role == NetwRegimePeer.ROLE_HOST:
		var config := NetwHostConfig.new()
		var enet := NetwENetParams.new()
		enet.port = port
		config.transport = enet
		config.server_name = "Racing regime"
		config.max_players = 2
		return NetwConnector.error_of(
			await NetwConnector.of(_api).host(payload, config),
		)
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": port }
	return NetwConnector.error_of(
		await NetwConnector.of(_api).join(target, payload),
	)


func _await_local_ready() -> bool:
	var deadline := Time.get_ticks_msec() + 10000
	while _api.local_player == null and Time.get_ticks_msec() < deadline:
		await _session.get_tree().process_frame
	if _api.local_player == null:
		return false
	# Only a client calibrates against a remote authority. The host is the
	# time authority and never pings itself, so it is born synchronized.
	while not _api.is_server() and not _api.clock.is_synchronized \
			and Time.get_ticks_msec() < deadline:
		await _session.get_tree().process_frame
	_attach_contact_probe()
	_track_travel.call_deferred()
	return _api.is_server() or _api.clock.is_synchronized


# Arms the local car's contact sampler for every scripted run, so a
# contact-free arm asserts its gesture stayed clean instead of assuming it.
func _attach_contact_probe() -> void:
	var car := _car()
	if car == null:
		return
	var sphere: RigidBody3D = car.sphere
	if sphere.get_script() == null:
		sphere.set_script(VehicleContactProbe)
		sphere.max_contacts_reported = maxi(sphere.max_contacts_reported, 16)


# What the gesture actually did: how far it drove and how much of the run it
# spent against a wall or another car.
func _condition_evidence() -> Dictionary:
	var out := { "gesture_travel": _travel }
	var car := _car()
	if car == null:
		return out
	var probe := car.sphere as VehicleContactProbe
	if probe:
		var frames := maxi(1, probe.contact_sequence)
		out["wall_contact_fraction"] = \
				float(probe.wall_contact_frames) / frames
		out["car_contact_fraction"] = \
				float(probe.car_contact_frames) / frames
	# Only wall_once populates these, and only it can be gated on them.
	if _graze_ticks >= 0:
		out["graze_contact_ticks"] = _graze_ticks
	if _after_ticks >= 0:
		out["after_contact_ticks"] = _after_ticks
	return out


# Accumulates path length rather than displacement, because a lap ends where
# it started and a displacement of zero would read as a car that never moved.
func _track_travel() -> void:
	var last := Vector3.INF
	while is_instance_valid(_session):
		await _session.get_tree().create_timer(0.25).timeout
		var car := _car()
		if car == null:
			continue
		var pos: Vector3 = car.sphere_position
		if last != Vector3.INF:
			_travel += (pos - last).length()
		last = pos


func _summary_handles() -> Dictionary:
	var handles := { }
	for node: Node in _vehicles():
		var entity := NetwEntity.of(node)
		if entity and entity.prediction:
			handles["car%d_%s" % [entity.controller, entity.entity_id]] = \
					entity.prediction
	return handles


func _vehicles() -> Array[Node]:
	var out: Array[Node] = []
	for node in _session.get_tree().root.find_children(
			"*",
			"Node3D",
			true,
			false,
	):
		if node is Vehicle:
			out.append(node)
	return out


func _car() -> Node:
	if _api.local_player == null:
		return null
	var car := _api.local_player.owner
	return car if is_instance_valid(car) else null


# The nearest Vehicle that is not the local car, or null while only one
# exists.
func _other_vehicle(car: Node) -> Node:
	for node: Node in _vehicles():
		if node != car and is_instance_valid(node):
			return node
	return null


# Untyped on purpose: a gesture can outlive its car when the other peer ends
# the session, and a freed instance fails a typed parameter before the guard
# inside can run.
func _clear_inputs(car) -> void:
	if not is_instance_valid(car):
		return
	car.inputs.state[car.inputs.accelerate] = false
	car.inputs.state[car.inputs.brake] = false
	car.inputs.state[car.inputs.steer_right] = false
	car.inputs.state[car.inputs.steer_left] = false


func _hold(seconds: float) -> void:
	await _session.get_tree().create_timer(seconds).timeout


# Sustained cornering with alternating steer legs, the scripted stand-in for
# human laps: throttle never lifts and the car spends the whole horizon
# turning, which is the regime the steering-side fields fork under.
func _drive_laps(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var steer_right := true
	car.inputs.state[car.inputs.accelerate] = true
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		car.inputs.state[car.inputs.steer_right] = steer_right
		car.inputs.state[car.inputs.steer_left] = not steer_right
		steer_right = not steer_right
		var leg := minf(
			LAPS_LEG_SECONDS,
			(deadline - Time.get_ticks_msec()) / 1000.0,
		)
		if leg <= 0.0:
			break
		await _session.get_tree().create_timer(leg).timeout
	_clear_inputs(car)


# The original felt-defect gesture: accelerate and release cycles for the
# first half, rotating in place for the second. The release gaps are what
# episode closes are timed against. Drive direction alternates each cycle so
# the car shuttles in place instead of marching into the outer wall, which
# the contact fraction proved a one-way version spends most of the run
# grinding against.
func _drive_symptom(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	var half := Time.get_ticks_msec() + int(seconds * 500.0)
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var forward := true
	while Time.get_ticks_msec() < half and is_instance_valid(car):
		var pedal: StringName = car.inputs.accelerate if forward \
				else car.inputs.brake
		forward = not forward
		car.inputs.state[pedal] = true
		await _session.get_tree().create_timer(SYMPTOM_DRIVE_SECONDS).timeout
		car.inputs.state[pedal] = false
		await _session.get_tree().create_timer(SYMPTOM_RELEASE_SECONDS).timeout
	var steer_right := true
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		car.inputs.state[car.inputs.steer_right] = steer_right
		car.inputs.state[car.inputs.steer_left] = not steer_right
		steer_right = not steer_right
		await _session.get_tree().create_timer(SYMPTOM_ROTATE_SECONDS).timeout
	_clear_inputs(car)


# The three-leg slalom: a sustained turn reversed twice, which curves the car
# without ever putting it near another body.
func _drive_slalom(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	var leg := seconds / 3.0
	car.inputs.state[car.inputs.accelerate] = true
	car.inputs.state[car.inputs.steer_right] = true
	await _session.get_tree().create_timer(leg).timeout
	car.inputs.state[car.inputs.steer_right] = false
	car.inputs.state[car.inputs.steer_left] = true
	await _session.get_tree().create_timer(leg).timeout
	car.inputs.state[car.inputs.steer_left] = false
	car.inputs.state[car.inputs.steer_right] = true
	await _session.get_tree().create_timer(leg).timeout
	_clear_inputs(car)


# Full throttle, no steering, until the track's outer wall stops the car. The
# wall repro wants the contact to be with the static world and nothing else.
func _drive_wall(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	car.inputs.state[car.inputs.accelerate] = true
	await _session.get_tree().create_timer(seconds).timeout
	_clear_inputs(car)


# Sustained wall contact with steer held into the wall, which is what an
# angular-velocity question needs and what a head-on rest cannot supply. Full
# throttle alone settles the car square against the wall with no angular
# content: it steers on none of its contact ticks, against a third to a half of
# them for a human driving the same repro.
#
# Closed loop on the contact probe rather than timed steer legs, for the reason
# _drive_at_other_car is closed loop. An open-loop version measured 5% wall
# contact against the head-on arm's 88%, and five times its path length: at
# full throttle a steer leg turns the car off the wall and drives it away,
# clipping the wall on part of each arc. So the steer direction is held while
# contact holds, and reverses only once contact is actually lost, which pins
# the car against the wall while steering the whole time.
func _drive_wall_grind(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	var probe := car.sphere as VehicleContactProbe
	if probe == null:
		push_error("regime: wall_grind needs the contact probe to close its loop")
		return
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var approach := Time.get_ticks_msec() \
			+ int(WALL_GRIND_APPROACH_SECONDS * 1000.0)
	car.inputs.state[car.inputs.accelerate] = true

	# Arrive square, the way the head-on arm reliably does, before steering at
	# all. Steering during the approach curves the car away from the wall it
	# has not reached yet.
	while (Time.get_ticks_msec() < approach
			and Time.get_ticks_msec() < deadline
			and is_instance_valid(car)
			and probe.wall_contacts == 0):
		await _session.get_tree().process_frame

	var steer_right := true
	var lost := 0
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		if probe.wall_contacts > 0:
			lost = 0
		else:
			lost += 1
			if lost >= WALL_GRIND_REACQUIRE_FRAMES:
				steer_right = not steer_right
				lost = 0
		car.inputs.state[car.inputs.steer_right] = steer_right
		car.inputs.state[car.inputs.steer_left] = not steer_right
		await _session.get_tree().process_frame
	_clear_inputs(car)


# One grazing wall hit, then driving on. This is the manual repro's SHAPE,
# which no other arm here reproduces, and the shape is the whole point.
#
# Measured 2026-07-29, the rendered wall_grind arm against the rendered manual
# capture it was standing in for: 4.4% of frames demoted against 74.2%, and
# quarantine targets of 6-18 against a ladder that doubled to its 256 cap. The
# grind is not a weaker version of the manual run. It is a different regime.
# Sustained contact keeps the divergence ATTRIBUTED, and an attributed
# re-breach does not escalate the flap; the manual capture's contact ended at
# 8.9 s and the remaining 23 s of demotion were re-breaches with nothing left
# to charge them to, which is what UNKNOWN attribution escalates on.
#
# So the felt spiral needs contact to END while the divergence outlives it. The
# gesture grazes with steer held in, the way a human clips a wall mid-corner,
# then reverses clear and drives laps for the rest of the horizon. What follows
# the hit is not filler: it is the interval the fallback rhythm is measured on,
# and a car parked after its hit would demote once and stop.
func _drive_wall_once(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	var probe := car.sphere as VehicleContactProbe
	if probe == null:
		push_error("regime: wall_once needs the contact probe to close its loop")
		return
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var approach := Time.get_ticks_msec() \
			+ int(WALL_GRIND_APPROACH_SECONDS * 1000.0)
	car.inputs.state[car.inputs.accelerate] = true

	# Arrive square before steering, for _drive_wall_grind's reason: a steer
	# during the approach curves the car away from the wall it has not reached.
	while (Time.get_ticks_msec() < approach
			and Time.get_ticks_msec() < deadline
			and is_instance_valid(car)
			and probe.wall_contacts == 0):
		await _session.get_tree().process_frame

	# The graze itself. Steer into the wall so the contact carries angular
	# content: a square rest produces a normal impulse with little torque, and
	# sphere_angular_velocity cannot fork without torque (root cause §4).
	#
	# Closed loop, for _drive_wall_grind's reason and because an open-loop
	# version was measured failing here specifically: holding steer_right blind
	# for two seconds bought 9 contact ticks, because the car bounced off and
	# spent the window driving away from a wall it had already left. The manual
	# capture this arm reproduces pressed for 44 ticks inside ONE second, and
	# that difference is the difference between a fork and no fork at all --
	# every blind-graze run measured p90 divergence of 0.000 against the manual
	# capture's 0.867. So the press is held the way the grind holds it, and only
	# the window is bounded.
	_graze_ticks = probe.wall_contact_frames
	var graze := Time.get_ticks_msec() + int(WALL_ONCE_GRAZE_SECONDS * 1000.0)
	var lost := 0
	var into_wall := true
	while (Time.get_ticks_msec() < graze
			and Time.get_ticks_msec() < deadline
			and is_instance_valid(car)):
		if probe.wall_contacts > 0:
			lost = 0
		else:
			lost += 1
			if lost >= WALL_GRIND_REACQUIRE_FRAMES:
				into_wall = not into_wall
				lost = 0
		car.inputs.state[car.inputs.steer_right] = into_wall
		car.inputs.state[car.inputs.steer_left] = not into_wall
		await _session.get_tree().process_frame
	_graze_ticks = probe.wall_contact_frames - _graze_ticks

	# Leave, and mean it. Reverse with the steer reversed too, so the car backs
	# off the wall on an arc instead of sliding along it.
	car.inputs.state[car.inputs.accelerate] = false
	car.inputs.state[car.inputs.steer_right] = false
	car.inputs.state[car.inputs.brake] = true
	car.inputs.state[car.inputs.steer_left] = true
	var backoff := Time.get_ticks_msec() \
			+ int(WALL_ONCE_BACKOFF_SECONDS * 1000.0)
	while (Time.get_ticks_msec() < backoff
			and Time.get_ticks_msec() < deadline
			and is_instance_valid(car)):
		await _session.get_tree().process_frame
	car.inputs.state[car.inputs.brake] = false
	car.inputs.state[car.inputs.steer_left] = false

	# Drive out the rest of the horizon on alternating steer legs, the way laps
	# does, but keeping clear of both the wall it just left and the parked car.
	# Neither repeller is precautionary; both answer a measured failure of the
	# plain laps gesture here. It drove into the host at 34.3 s of a 35 s run,
	# and this arm bars car contact outright for the reason wall_grind does -- a
	# SINGLE tick against the other car moves a whole run's divergence. And it
	# clipped the wall repeatedly, which put 46 and 44 contact ticks into single
	# seconds late in a run whose entire premise is that the contact is OVER.
	# Contact after the graze does not merely add noise: it re-attributes the
	# divergence, and an attributed re-breach is the one thing this arm exists
	# to keep out of the aftermath.
	_after_ticks = probe.wall_contact_frames
	var steer_right := true
	var peeling := false
	var clear_frames := 0
	var leg_end := Time.get_ticks_msec() + int(LAPS_LEG_SECONDS * 1000.0)
	car.inputs.state[car.inputs.accelerate] = true
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		var yielded := false
		var target := _other_vehicle(car)
		if target:
			var to_target: Vector3 = target.sphere_position - car.sphere_position
			var gap := to_target.length()
			if gap < WALL_ONCE_CLEARANCE:
				# The inverse of _drive_at_other_car's closed loop: the same
				# bearing, steered the other way, so the car turns off the
				# collision course rather than onto it.
				var want := atan2(to_target.x, to_target.z)
				var error := angle_difference(car.heading, want)
				car.inputs.state[car.inputs.steer_right] = error >= 0.0
				car.inputs.state[car.inputs.steer_left] = error < 0.0
				var closing := gap < WALL_ONCE_YIELD
				car.inputs.state[car.inputs.accelerate] = not closing
				car.inputs.state[car.inputs.brake] = closing
				yielded = true
		if not yielded:
			car.inputs.state[car.inputs.accelerate] = true
			car.inputs.state[car.inputs.brake] = false
			# Peel off the wall on the RISING edge of contact and hold that
			# direction until the car is properly clear, rather than flipping
			# per frame. Flipping per frame was measured pinning the car
			# against the wall for 854-1392 ticks of aftermath: a steer that
			# reverses every frame integrates to no steer at all, so the car
			# leant on the wall for the rest of the run. Same reason
			# _drive_wall_grind counts frames before reversing -- solver contact
			# flickers, and a per-frame response chatters instead of steering.
			if probe.wall_contacts > 0:
				if not peeling:
					peeling = true
					steer_right = not steer_right
				clear_frames = 0
			elif peeling:
				clear_frames += 1
				if clear_frames >= WALL_GRIND_REACQUIRE_FRAMES:
					peeling = false
					leg_end = Time.get_ticks_msec() \
							+ int(LAPS_LEG_SECONDS * 1000.0)
			if not peeling and Time.get_ticks_msec() >= leg_end:
				steer_right = not steer_right
				leg_end = Time.get_ticks_msec() + int(LAPS_LEG_SECONDS * 1000.0)
			car.inputs.state[car.inputs.steer_right] = steer_right
			car.inputs.state[car.inputs.steer_left] = not steer_right
		await _session.get_tree().process_frame
	_after_ticks = probe.wall_contact_frames - _after_ticks
	_clear_inputs(car)


# Repeated high-speed square hits into the outer wall: accelerate to top
# speed, detect the impact by the model velocity collapsing, reverse briefly
# to back off, then hit again. This maximizes clean hits rather than
# sustained contact, which a single scripted approach never reproduces.
func _drive_wall_hard(seconds: float) -> void:
	const MOVING_SPEED := 2.0
	const IMPACT_SPEED := 0.5
	const FORWARD_TIMEOUT := 4.0
	const BACKOFF := 0.8
	var car := _car()
	if car == null:
		return
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var hits := 0
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		car.inputs.state[car.inputs.accelerate] = true
		car.inputs.state[car.inputs.brake] = false
		var leg_deadline := Time.get_ticks_msec() + int(FORWARD_TIMEOUT * 1000.0)
		var was_moving := false
		while (Time.get_ticks_msec() < leg_deadline
				and Time.get_ticks_msec() < deadline and is_instance_valid(car)):
			var speed: float = car.linear_velocity.length()
			was_moving = was_moving or speed > MOVING_SPEED
			if was_moving and speed < IMPACT_SPEED:
				hits += 1
				break
			await _session.get_tree().process_frame
		car.inputs.state[car.inputs.accelerate] = false
		car.inputs.state[car.inputs.brake] = true
		var back_deadline := Time.get_ticks_msec() + int(BACKOFF * 1000.0)
		while (Time.get_ticks_msec() < back_deadline
				and Time.get_ticks_msec() < deadline and is_instance_valid(car)):
			await _session.get_tree().process_frame
	_clear_inputs(car)
	print("[regime] wall_hard hits=%d" % hits)


# Steers toward the other car every frame until the run ends.
#
# Closed loop rather than a scripted heading, because the two cars spawn on
# grid slots the capture does not choose and an open-loop guess that missed
# would produce a contact-free run wearing a contact run's name.
func _drive_at_other_car(seconds: float) -> void:
	var car := _car()
	if car == null:
		return
	car.inputs.state[car.inputs.accelerate] = true
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		var target := _other_vehicle(car)
		if target:
			var to_target: Vector3 = target.sphere_position - car.sphere_position
			# The sphere rolls along +Z at heading zero, opposite the model's
			# facing, so the bearing is taken without negating the delta.
			var want := atan2(to_target.x, to_target.z)
			var error := angle_difference(car.heading, want)
			car.inputs.state[car.inputs.steer_left] = error > 0.05
			car.inputs.state[car.inputs.steer_right] = error < -0.05
		await _session.get_tree().process_frame
	_clear_inputs(car)
