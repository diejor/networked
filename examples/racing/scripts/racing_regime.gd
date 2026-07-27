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
# The symptom gesture's cycle, matching the reported repro: accelerate one to
# two seconds, release, wait for the episode to close, repeat.
const SYMPTOM_DRIVE_SECONDS := 1.5
const SYMPTOM_RELEASE_SECONDS := 2.5
const SYMPTOM_ROTATE_SECONDS := 2.0

var _session: Node
var _api: NetwMultiplayer
var _travel := 0.0


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
		config.scheme = &"enet"
		config.server_name = "Racing regime"
		config.max_players = 2
		config.params = { "port": port }
		return await _api.host(payload, config)
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": port }
	return await _api.join(target, payload, 10.0)


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
