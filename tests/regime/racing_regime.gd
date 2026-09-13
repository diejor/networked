class_name RacingRegime
extends RefCounted

const TRAVEL_SAMPLE_SECONDS := 0.25

var session: Node
var api: NetwMultiplayer
var session_handle: NetwSessionHandle
var regime: NetwRegimePeer
var travel := 0.0
var car_travel: Dictionary[String, float] = { }
var car_model_travel: Dictionary[String, float] = { }
var car_seen: Dictionary[String, int] = { }


static func armed() -> bool:
	return NetwRegimePeer.armed()


static func attach(session: Node, api: NetwMultiplayer) -> NetwRegimePeer:
	var adapter := RacingRegime.new()
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
		"laps": adapter.drive_laps,
		"hold": adapter.hold,
	}
	session.add_child(peer)
	peer.configure_from_args()
	peer.set_meta(&"racing_adapter", adapter)
	adapter.regime = peer
	return peer


func connect_session(role: String, port: int) -> Error:
	var peer := ENetMultiplayerPeer.new()
	var err: Error
	if NetwRegimePeer.role_is_host(role):
		var info := NetwServerInfo.new()
		info.motd = "Racing regime"
		info.max_players = regime.peers
		session_handle.set_server_info(info)
		err = peer.create_server(port, regime.peers)
	else:
		err = peer.create_client("127.0.0.1", port)
	if err != OK:
		return err
	Netw.join(session, StringName(role))
	api.multiplayer_peer = peer
	if api.multiplayer_peer != peer:
		return ERR_CANT_CONNECT
	return OK


func await_local_ready() -> bool:
	var deadline := Time.get_ticks_msec() + 40000
	while session_handle.participants.size() < regime.peers \
			and Time.get_ticks_msec() < deadline:
		await session.get_tree().process_frame
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
	var last_body: Dictionary[String, Vector3] = { }
	var last_model: Dictionary[String, Vector3] = { }
	while is_instance_valid(session):
		await session.get_tree().create_timer(TRAVEL_SAMPLE_SECONDS).timeout
		var car := local_car()
		if car:
			var here := car.get_vehicle_position()
			if last_car != Vector3.INF:
				travel += (here - last_car).length()
			last_car = here
		for each: Vehicle in cars():
			var key := car_key(each)
			if key.is_empty():
				continue
			car_seen[key] = car_seen.get(key, 0) + 1
			var body: Vector3 = each.display_position
			if last_body.has(key):
				car_travel[key] = car_travel.get(key, 0.0) \
						+ (body - last_body[key]).length()
			last_body[key] = body
			var model: Node3D = each.vehicle_model
			if model:
				var drawn := model.global_position
				if last_model.has(key):
					car_model_travel[key] = car_model_travel.get(key, 0.0) \
							+ (drawn - last_model[key]).length()
				last_model[key] = drawn


func car_key(car: Vehicle) -> String:
	var entity := NetwEntity.of(car)
	return "car%d_%s" % [entity.controller, entity.entity_id] if entity else ""


func drive_laps(seconds: float) -> void:
	hold_action(&"forward", true)
	hold_action(&"left", true)
	await session.get_tree().create_timer(seconds).timeout
	hold_action(&"forward", false)
	hold_action(&"left", false)


func hold_action(action: StringName, pressed: bool) -> void:
	var event := InputEventAction.new()
	event.action = action
	event.pressed = pressed
	event.strength = 1.0 if pressed else 0.0
	Input.parse_input_event(event)


func hold(seconds: float) -> void:
	await session.get_tree().create_timer(seconds).timeout


func condition_evidence() -> Dictionary:
	return {
		"gesture_travel": travel,
		"cars_seen": car_seen.size(),
		"car_travel": car_travel,
		"car_model_travel": car_model_travel,
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
	for car: Vehicle in cars():
		var entity := NetwEntity.of(car)
		if entity and entity.prediction:
			handles[car_key(car)] = entity.prediction
	return handles


func local_car() -> Vehicle:
	var player: NetwEntity = session_handle.local_player
	return player.owner as Vehicle if player else null


func race_level() -> Node:
	var here: NetwParticipant = session_handle.local_participant
	var scene: NetwSceneHandle = here.current_scene if here else null
	return scene.root if scene else null


func cars() -> Array[Vehicle]:
	var out: Array[Vehicle] = []
	var level := race_level()
	if level == null:
		return out
	for node in level.find_children("*", "Node3D", true, false):
		if node is Vehicle:
			out.append(node)
	return out
