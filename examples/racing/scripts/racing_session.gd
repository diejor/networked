extends Node
## Tree-less session bootstrap for the racing example.
##
## The example carries no [MultiplayerTree] node. This root script registers the
## session's [NetwSessionConfig] and a single-scene [NetwSceneConfig] whose only
## declared scene is the track, so joining a host through the child
## [ConnectBrowser] admits the participant straight onto the track and spawns
## their car. The session is already mounted when this runs, by the
## [code]networked/install_as_default[/code] autoload at startup.

const TRACK := preload("res://examples/racing/scenes/track.tscn")
const CAPTURE_ROLE_VAR := "NETW_RACING_CAPTURE_ROLE"
const CAPTURE_PORT_VAR := "NETW_RACING_CAPTURE_PORT"
const CAPTURE_SECONDS_VAR := "NETW_RACING_CAPTURE_SECONDS"
const CAPTURE_MODE_VAR := "NETW_RACING_CAPTURE_MODE"
const SIMULATE_NEAREST_VAR := "NETW_RACING_SIMULATE_NEAREST"

## Simulates the nearest replicated opponent inside each local car's island.
@export var simulate_nearest_opponent := false

@onready var _browser: ConnectBrowser = %ConnectBrowser

var api: NetwMultiplayer:
	get:
		return multiplayer


func _enter_tree() -> void:
	var session := NetwSessionConfig.new()
	session.app_id = &"netw-example-racing"
	api.object_configuration_add(self, session)

	var scenes := NetwSceneConfig.new()
	scenes.concurrency = NetwSceneConfig.Concurrency.SINGLE
	scenes.initial_scenes = [TRACK]
	scenes.scenes = { &"Track": TRACK }
	api.object_configuration_add(self, scenes)


func _ready() -> void:
	api.participant_joined.connect(_on_participant_joined)
	api.scenes.scene_spawned.connect(_on_scene_spawned)
	api.local_scene_changed.connect(_on_local_scene_changed)
	api.session_ended.connect(_show_browser)
	api.server_disconnected.connect(_show_browser)
	if not OS.get_environment(CAPTURE_ROLE_VAR).is_empty():
		_run_scripted_capture.call_deferred()


func _on_participant_joined(participant: NetwParticipant) -> void:
	var track: MultiplayerScene = api.scenes.scene(&"Track")
	if api.is_server() and is_instance_valid(track):
		track.admit(participant)


# The listen host is accepted before the startup scene spawns, so its join
# admission finds no scene. Admission re-runs when the scene arrives, keeping
# join order and scene order decoupled.
func _on_scene_spawned(scene: MultiplayerScene) -> void:
	# Every car on the track shares one approximate island, so contact against
	# another car classifies as a boundary breach instead of an undeclared
	# contact. Promotion to simulated fidelity stays opt-in.
	var island := scene.prediction_island() \
			.approximate() \
			.from_interest()
	if simulate_nearest_opponent \
			or not OS.get_environment(SIMULATE_NEAREST_VAR).is_empty():
		island.simulate_nearest(1)
	if not api.is_server():
		return
	for participant: NetwParticipant in api.participants:
		if participant.current_scene == null:
			scene.admit(participant)


# The browser steps aside once the local participant is racing and returns when
# the session ends.
func _on_local_scene_changed(_from: MultiplayerScene, to: MultiplayerScene) -> void:
	_browser.visible = to == null


func _show_browser() -> void:
	_browser.visible = true


# Runs an inert-by-default ENet capture endpoint for the two-process acceptance
# script. The client holds a sustained forward-right turn, then both exit.
func _run_scripted_capture() -> void:
	var role := OS.get_environment(CAPTURE_ROLE_VAR).to_lower()
	var port := OS.get_environment(CAPTURE_PORT_VAR).to_int()
	if port <= 0:
		port = 30190
	var payload := JoinPayload.new()
	payload.username = &"mario" if role == "host" else &"luigi"

	var error := ERR_INVALID_PARAMETER
	if role == "host":
		var config := NetwHostConfig.new()
		config.scheme = &"enet"
		config.server_name = "Racing capture"
		config.max_players = 2
		config.params = { "port": port }
		error = await api.host(payload, config)
	elif role == "client":
		var target := NetwConnectTarget.new()
		target.scheme = &"enet"
		target.address = "127.0.0.1"
		target.metadata = { "port": port }
		error = await api.join(target, payload, 10.0)
	if error != OK:
		push_error("Racing capture %s failed with %s" % [role, error_string(error)])
		get_tree().quit(error)
		return

	var deadline := Time.get_ticks_msec() + 10000
	while api.local_player == null and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	if api.local_player == null:
		push_error("Racing capture %s timed out waiting for its car" % role)
		get_tree().quit(ERR_TIMEOUT)
		return

	print("[capture] %s ready on port %d" % [role, port])
	var car := api.local_player.owner if api.local_player else null
	var seconds := OS.get_environment(CAPTURE_SECONDS_VAR).to_float()
	if seconds <= 0.0:
		seconds = 8.0
	var mode := OS.get_environment(CAPTURE_MODE_VAR).to_lower()
	if mode.is_empty():
		mode = "slalom"
	if role != "client":
		# The host holds still in every mode. A contact probe wants one moving
		# body and one stationary target, so the disturbance has a single cause.
		await get_tree().create_timer(seconds).timeout
	else:
		print("[capture] client driving %s for %.1fs" % [mode, seconds])
		match mode:
			"contact":
				await _drive_at_other_car(car, seconds)
			"wall":
				await _drive_straight(car, seconds)
			"wall_hard":
				await _drive_wall_hard(car, seconds)
			_:
				await _drive_slalom(car, seconds)
	if is_instance_valid(car):
		print(
			(
					"[capture] %s driven position=%s throttle=%s steer=%s "
					+ "corrections=%s"
			) % [
				role,
				car.sphere_position,
				car.inputs.throttle,
				car.inputs.steer,
				api.local_player.prediction.corrections,
			],
		)
	if role == "client" and is_instance_valid(car):
		car.inputs.state[car.inputs.accelerate] = false
		car.inputs.state[car.inputs.steer_right] = false
		car.inputs.state[car.inputs.steer_left] = false
	await get_tree().process_frame
	print("[capture] %s complete" % role)
	get_tree().quit(OK)


# The three-leg slalom: a sustained turn reversed twice, which curves the car
# without ever putting it near another body.
func _drive_slalom(car: Node, seconds: float) -> void:
	var leg := seconds / 3.0
	car.inputs.state[car.inputs.accelerate] = true
	car.inputs.state[car.inputs.steer_right] = true
	await get_tree().create_timer(leg).timeout
	car.inputs.state[car.inputs.steer_right] = false
	car.inputs.state[car.inputs.steer_left] = true
	await get_tree().create_timer(leg).timeout
	car.inputs.state[car.inputs.steer_left] = false
	car.inputs.state[car.inputs.steer_right] = true
	await get_tree().create_timer(leg).timeout


# Full throttle, no steering, until the track's outer wall stops the car. The
# wall repro wants the contact to be with the static world and nothing else.
func _drive_straight(car: Node, seconds: float) -> void:
	car.inputs.state[car.inputs.accelerate] = true
	await get_tree().create_timer(seconds).timeout


# Repeated high-speed square hits into the outer wall: accelerate to top speed,
# detect the impact by the model velocity collapsing, reverse briefly to back
# off, then hit again. The manual-repro signature is the correction burst each
# FRESH contact forks, so this maximizes clean hits rather than sustained
# contact, which a single scripted approach never reproduces (the wall driver
# above measures zero corrections against a static wall).
func _drive_wall_hard(car: Node, seconds: float) -> void:
	const MOVING_SPEED := 2.0    # must exceed this before a stop counts as a hit
	const IMPACT_SPEED := 0.5    # model speed below this after moving is an impact
	const FORWARD_TIMEOUT := 4.0 # a forward leg never runs longer than this
	const BACKOFF := 0.8         # seconds of reverse between hits
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var next_report := 0
	var hits := 0
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		# Accelerate into the wall until the body stops or the leg times out.
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
			if Time.get_ticks_msec() >= next_report:
				next_report = Time.get_ticks_msec() + 1000
				print("[wall_hard] hits=%d speed=%.2f corrections=%s" % [
					hits, speed, _local_corrections(),
				])
			await get_tree().process_frame
		# Back off so the next hit is a fresh square impact, not a wall slide.
		car.inputs.state[car.inputs.accelerate] = false
		car.inputs.state[car.inputs.brake] = true
		var back_deadline := Time.get_ticks_msec() + int(BACKOFF * 1000.0)
		while (Time.get_ticks_msec() < back_deadline
				and Time.get_ticks_msec() < deadline and is_instance_valid(car)):
			await get_tree().process_frame
	car.inputs.state[car.inputs.accelerate] = false
	car.inputs.state[car.inputs.brake] = false
	print("[wall_hard] done hits=%d corrections=%s" % [hits, _local_corrections()])


# The local car's running correction count, or -1 before its prediction exists.
func _local_corrections() -> int:
	if api.local_player and api.local_player.prediction:
		return api.local_player.prediction.corrections
	return -1


# Steers toward the other car every frame until the run ends.
#
# Closed loop rather than a scripted heading, because the two cars spawn on grid
# slots the capture does not choose and an open-loop guess that missed would
# produce a contact-free run wearing a contact run's name.
func _drive_at_other_car(car: Node, seconds: float) -> void:
	car.inputs.state[car.inputs.accelerate] = true
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	var next_report := 0
	while Time.get_ticks_msec() < deadline and is_instance_valid(car):
		var target := _other_vehicle(car)
		if target:
			var to_target: Vector3 = target.sphere_position - car.sphere_position
			# The sphere rolls along +Z at heading zero, opposite the model's
			# facing, so the bearing is taken without negating the delta. Aiming
			# by the facing instead reads a near-zero error while the car drives
			# steadily away, which is a contact-free run wearing a contact name.
			var want := atan2(to_target.x, to_target.z)
			var error := angle_difference(car.heading, want)
			car.inputs.state[car.inputs.steer_left] = error > 0.05
			car.inputs.state[car.inputs.steer_right] = error < -0.05
			if Time.get_ticks_msec() >= next_report:
				next_report = Time.get_ticks_msec() + 1000
				print("[aim] dist=%.2f heading=%.2f want=%.2f error=%.2f" % [
					to_target.length(), car.heading, want, error,
				])
		elif Time.get_ticks_msec() >= next_report:
			next_report = Time.get_ticks_msec() + 1000
			print("[aim] no target found")
		await get_tree().process_frame


# The nearest [Vehicle] that is not [param car], or null while only one exists.
func _other_vehicle(car: Node) -> Node:
	for node in get_tree().get_nodes_in_group("vehicles"):
		if node != car and is_instance_valid(node):
			return node
	for node in get_tree().root.find_children("*", "Node3D", true, false):
		if node is Vehicle and node != car:
			return node
	return null
