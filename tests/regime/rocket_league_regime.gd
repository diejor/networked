class_name RocketLeagueRegime
extends RefCounted

const TRAVEL_SAMPLE_SECONDS := 0.25

var session: Node
var api: NetwMultiplayer
var session_handle: NetwSessionHandle
var regime: NetwRegimePeer
var travel := 0.0
var ball_travel := 0.0
var car_travel: Dictionary[String, float] = { }
var car_model_travel: Dictionary[String, float] = { }
var car_wheel_gap: Dictionary[String, float] = { }
var car_tilt: Dictionary[String, float] = { }
var car_seen: Dictionary[String, int] = { }
var car_state_gap: Dictionary[String, float] = { }
var car_frozen: Dictionary[String, bool] = { }
var car_spin: Dictionary[String, float] = { }


static func armed() -> bool:
	return NetwRegimePeer.armed()


static func attach(session: Node, api: NetwMultiplayer) -> NetwRegimePeer:
	var adapter := RocketLeagueRegime.new()
	adapter.session = session
	adapter.api = api
	adapter.session_handle = Netw.session(session)

	var peer := NetwRegimePeer.new()
	peer.api = api
	peer.connect_session = adapter.connect_session
	peer.await_local_ready = adapter.await_local_ready
	peer.summary_handles = adapter.summary_handles
	peer.condition_evidence = adapter.condition_evidence
	peer.gestures = {
		"chase": adapter.drive_chase,
		"hold": adapter.hold,
	}
	session.add_child(peer)
	peer.configure_from_args()
	peer.set_meta(&"rocket_league_adapter", adapter)
	adapter.regime = peer
	return peer


func connect_session(role: String, port: int) -> Error:
	var username := StringName(role)
	var peer := ENetMultiplayerPeer.new()
	var err: Error
	if NetwRegimePeer.role_is_host(role):
		var info := NetwServerInfo.new()
		info.motd = "Rocket league regime"
		info.max_players = regime.peers
		session_handle.set_server_info(info)
		err = peer.create_server(port, regime.peers)
	else:
		err = peer.create_client("127.0.0.1", port)
	if err != OK:
		return err
	Netw.join(session, username)
	api.multiplayer_peer = peer
	if api.multiplayer_peer != peer:
		return ERR_CANT_CONNECT
	return OK


func await_local_ready() -> bool:
	var deadline := Time.get_ticks_msec() + 40000
	while session_handle.participants.size() < regime.peers \
			and Time.get_ticks_msec() < deadline:
		await session.get_tree().process_frame
	if session_handle.participants.size() < regime.peers:
		return false

	if NetwRegimePeer.role_is_host(regime.role):
		await session.get_tree().create_timer(1.0).timeout
		start_match()

	while local_car() == null and Time.get_ticks_msec() < deadline:
		await session.get_tree().process_frame
	if local_car() == null:
		return false

	var clock: NetwClockHandle = Netw.clock(session)
	while not api.is_server() and not clock.is_synchronized \
			and Time.get_ticks_msec() < deadline:
		await session.get_tree().process_frame

	track_travel.call_deferred()
	return api.is_server() or clock.is_synchronized


func track_travel() -> void:
	var last_car := Vector3.INF
	var last_ball := Vector3.INF
	var last_body: Dictionary[String, Vector3] = { }
	var last_model: Dictionary[String, Vector3] = { }
	var last_spin: Dictionary[String, Basis] = { }
	while is_instance_valid(session):
		await session.get_tree().create_timer(TRAVEL_SAMPLE_SECONDS).timeout
		var car := local_car()
		if car:
			var here := car.global_position
			if last_car != Vector3.INF:
				travel += (here - last_car).length()
			last_car = here
		var ball := arena_ball()
		if ball:
			var spot := ball.global_position
			if last_ball != Vector3.INF:
				ball_travel += (spot - last_ball).length()
			last_ball = spot
		for each: RocketCar in cars():
			var key := car_key(each)
			if key.is_empty():
				continue
			car_seen[key] = car_seen.get(key, 0) + 1
			var body := each.global_position
			if last_body.has(key):
				car_travel[key] = car_travel.get(key, 0.0) \
						+ (body - last_body[key]).length()
			last_body[key] = body
			var model: Node3D = each.get_node_or_null(^"Car_model")
			if model:
				var drawn := model.global_position
				if last_model.has(key):
					car_model_travel[key] = car_model_travel.get(key, 0.0) \
							+ (drawn - last_model[key]).length()
				last_model[key] = drawn
			car_wheel_gap[key] = wheel_gap(each)
			car_state_gap[key] = (each.pose.origin - each.global_position) \
					.length()
			car_frozen[key] = each.freeze
			var turn := each.global_basis.orthonormalized()
			if last_spin.has(key):
				car_spin[key] = car_spin.get(key, 0.0) + rad_to_deg(
					last_spin[key].get_rotation_quaternion().angle_to(
						turn.get_rotation_quaternion(),
					),
				)
			last_spin[key] = turn
			car_tilt[key] = rad_to_deg(
				each.global_basis.y.angle_to(Vector3.UP),
			)


func wheel_gap(car: RocketCar) -> float:
	var centre := Vector3.ZERO
	var counted := 0
	for wheel: Node3D in car.get_node(^"Wheels").get_children():
		centre += wheel.global_position
		counted += 1
	if counted == 0:
		return -1.0
	return (centre / counted - car.global_position).length()


func car_key(car: RocketCar) -> String:
	var entity := NetwEntity.of(car)
	return "car%d_%s" % [entity.controller, entity.entity_id] if entity else ""


func drive_chase(seconds: float) -> void:
	var car := local_car()
	if car:
		car.inputs.ai_enabled = true
	await session.get_tree().create_timer(seconds).timeout
	if car:
		car.inputs.ai_enabled = false
		car.inputs.ai_motion = Vector2.ZERO
		car.inputs.ai_jumping = false


func hold(seconds: float) -> void:
	await session.get_tree().create_timer(seconds).timeout


func condition_evidence() -> Dictionary:
	return {
		"gesture_travel": travel,
		"ball_travel": ball_travel,
		"cars_seen": car_seen.size(),
		"car_travel": car_travel,
		"car_model_travel": car_model_travel,
		"car_wheel_gap": car_wheel_gap,
		"car_tilt": car_tilt,
		"car_state_gap": car_state_gap,
		"car_frozen": car_frozen,
		"car_spin": car_spin,
		"car_samples": car_seen,
		"remote_travel_min": remote_travel_min(),
		"participants": session_handle.participants.size(),
		"api_peers": Array(api.get_peers()),
	}


func remote_travel_min() -> float:
	var mine := car_key(local_car()) if local_car() else ""
	var least := -1.0
	for key: String in car_travel:
		if key == mine:
			continue
		var moved: float = car_travel[key]
		if least < 0.0 or moved < least:
			least = moved
	return least


func summary_handles() -> Dictionary:
	var handles := { }
	for car: RocketCar in cars():
		var entity := NetwEntity.of(car)
		if entity and entity.prediction:
			handles[car_key(car)] = entity.prediction
	return handles


func start_match() -> void:
	var lobby: NetwSceneHandle = Netw.scene(session, &"Lobby")
	if lobby == null:
		return
	var panel: InLobby = lobby.root.get_node_or_null(^"UI/InLobby")
	if panel:
		panel.on_start_pressed()


func local_car() -> RocketCar:
	var player: NetwEntity = session_handle.local_player
	return player.owner as RocketCar if player else null


func arena_level() -> Node:
	var arena: NetwSceneHandle = Netw.scene(session, &"Arena")
	return arena.root if arena else null


func arena_ball() -> RocketBall:
	var level := arena_level()
	return level.get_node_or_null(^"ball|0") as RocketBall if level else null


func cars() -> Array[RocketCar]:
	var out: Array[RocketCar] = []
	var level := arena_level()
	if level == null:
		return out
	for child in level.get_node(^"Players").get_children():
		if child is RocketCar:
			out.append(child)
	return out
