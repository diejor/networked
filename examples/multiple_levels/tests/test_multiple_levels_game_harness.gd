class_name TestMultipleLevelsGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/multiple_levels/Main.tscn")
const LEVEL_1 := preload("res://examples/multiple_levels/Level1.tscn")
const LEVEL_2 := preload("res://examples/multiple_levels/Level2.tscn")
const PLAYER := preload("res://examples/multiple_levels/Player.tscn")
const _LEVEL_1_PATH := "res://examples/multiple_levels/Level1.tscn"
const _LEVEL_2_PATH := "res://examples/multiple_levels/Level2.tscn"
const _TP_MARKER := ^"%Teleporter/Marker2D"

var game: NetwGameHarness


func before_test() -> void:
	MemoryDatabase.new().delete_slot_default(null, &"campaign")
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


func test_host_input_reaches_local_player_after_add_host() -> void:
	var valeria := await game.add_host("valeria", true)
	await _wait_for_transition(valeria)

	var player := valeria.local_player as Node2D
	assert_that(player).is_not_null()
	assert_that(player.visible).is_true()
	var start := player.position.x

	valeria.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	valeria.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_a_host_named_after_the_preplaced_player_keeps_a_readable_name() -> void:
	var host := await game.add_host("Player", true)
	await _wait_for_transition(host)

	var body := host.local_player as Node2D
	assert_that(body).is_not_null()
	var seated := String(body.name)
	assert_str(seated) \
			.override_failure_message(
				"the host's body was seated as '%s', which no spawner accepts"
				% seated,
			) \
			.is_equal(seated.validate_node_name())
	assert_that(NetwEntity.of(body).entity_id).is_equal(&"Player")


func test_client_input_reaches_local_player() -> void:
	await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transition(jose)

	var player := jose.local_player as Node2D
	assert_that(player).is_not_null()
	var start := player.position.x

	await game.sync_ticks(16)
	jose.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(player.pressed[&"move_right"]).is_true()
	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	jose.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(player.pressed[&"move_right"]).is_false()
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_host_input_replicates_to_client() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transitions()

	var valeria_on_jose: Node2D = await jose.await_player(&"valeria", 2.0)
	var start := valeria_on_jose.position.x

	await game.sync_ticks(16)
	valeria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	valeria.simulate_action_release("move_right")
	await game.sync_ticks(16)

	assert_that(valeria_on_jose.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_rough_link_replicates_to_client() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transitions()

	var valeria_on_jose: Node2D = await jose.await_player(&"valeria", 2.0)
	var start := valeria_on_jose.position.x

	game.path(valeria, jose) \
			.loss(0.5) \
			.latency_ms(66.0) \
			.seed(1)

	await game.sync_ticks(16)
	valeria.simulate_action_press("move_right")
	await game.sync_ticks(32)
	valeria.simulate_action_release("move_right")
	await game.sync_ticks(64)

	assert_that(valeria_on_jose.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_show_views_displays_single_scene_participants() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)

	var display := game.show_views()
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_true()
	assert_that(display.has_slot(jose.slot)).is_true()
	assert_that(valeria.slot.visible).is_true()
	assert_that(jose.slot.visible).is_true()

	display.remove_slot(valeria.slot)
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_false()
	assert_that(valeria.slot.visible).is_false()


func test_show_views_can_be_called_before_adding_participants() -> void:
	var display := game.show_views()

	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_true()
	assert_that(display.has_slot(jose.slot)).is_true()


func test_host_scene_request_keeps_camera_current() -> void:
	var valeria := await game.add_host("valeria", true)
	await _wait_for_transition(valeria)

	var player := valeria.local_player as Node2D
	var camera := player.get_node("Camera2D") as Camera2D
	assert_that(player).is_not_null()
	assert_that(camera).is_not_null()
	assert_that(player.get_viewport().get_camera_2d()).is_equal(camera)

	var promise := await _request_level_2(valeria)
	var level_2 := await valeria.await_scene(&"Level2", 2.0)

	assert_that(promise.is_settled).is_true()
	assert_that(promise.code).is_equal(OK)
	assert_that(level_2).is_not_null()

	var arrived := await _wait_for_body_in(valeria, &"valeria", &"Level2")
	assert_that(arrived).is_not_null()
	var arrived_camera := arrived.get_node("Camera2D") as Camera2D
	assert_that(arrived_camera).is_not_null()
	assert_that(arrived.get_viewport().get_camera_2d()).is_equal(arrived_camera)


func test_clients_still_see_each_other_after_scene_change() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	var maria := await game.add_client("maria", true)
	await _wait_for_transitions()
	var maria_on_jose_start: Node2D = await jose.await_player(&"maria", 2.0)
	assert_that(maria_on_jose_start).is_not_null()
	var base_x := maria_on_jose_start.position.x
	maria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	maria.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(maria_on_jose_start.position.x).is_greater(base_x)

	for requester in [valeria, jose, maria]:
		var promise := await _request_level_2(requester)
		assert_that(promise.code).is_equal(OK)
	for runner in [valeria, jose, maria]:
		assert_that(await runner.await_scene(&"Level2", 2.0)).is_not_null()

	var maria_local := await _wait_for_body_in(maria, &"maria", &"Level2")
	var maria_on_valeria := await _wait_for_body_in(valeria, &"maria", &"Level2")
	var maria_on_jose := await _wait_for_body_in(jose, &"maria", &"Level2")
	assert_that(maria_local).is_not_null()
	assert_that(maria_on_valeria).is_not_null()
	assert_that(maria_on_jose).is_not_null()
	await game.sync_ticks(8)

	var local_start := maria_local.position.x
	var host_start := maria_on_valeria.position.x
	var jose_start := maria_on_jose.position.x

	maria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	maria.simulate_action_release("move_right")
	await game.sync_ticks(8)

	assert_that(maria_local.position.x) \
			.override_failure_message("maria's own local player did not move") \
			.is_greater(local_start)
	assert_that(maria_on_valeria.position.x) \
			.override_failure_message("host did not see maria move") \
			.is_greater(host_start)
	assert_that(maria_on_jose.position.x) \
			.override_failure_message("jose (client) did not see maria move") \
			.is_greater(jose_start)


func test_client_round_trip_teleport_stays_functional() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	var maria := await game.add_client("maria", true)
	await _wait_for_transitions()
	if OS.get_environment("NETW_RELOCATION_LINK") == "rough":
		game.path(jose, valeria).latency_ms(125.0).loss(0.5).seed(7)
		game.path(valeria, jose).latency_ms(125.0).loss(0.5).seed(8)
		game.path(valeria, maria).latency_ms(125.0).loss(0.5).seed(9)

	await _teleport_without_bridge(jose, valeria, _LEVEL_1_PATH, &"Level1")
	_assert_arrived_at_marker(jose, "Level1")

	await _teleport_without_bridge(jose, valeria, _LEVEL_2_PATH, &"Level2")
	assert_that(await jose.await_scene(&"Level2", 2.0)) \
			.override_failure_message("jose never reached Level2") \
			.is_not_null()
	_assert_arrived_at_marker(jose, "Level2")
	assert_that(Netw.session(jose.tree).presented_scene.label) \
			.override_failure_message("jose presents no Level2 after moving into it") \
			.is_equal(&"Level2")

	await _teleport_without_bridge(jose, maria, _LEVEL_1_PATH, &"Level1")
	await game.sync_ticks(20)
	_assert_arrived_at_marker(jose, "Level1")

	var jose_local := jose.local_player as Node2D
	assert_that(jose_local) \
			.override_failure_message("jose has no local player after return") \
			.is_not_null()
	var jose_scene: NetwSceneHandle = NetwEntity.of(jose_local).scene
	assert_bool(jose_scene.is_declared) \
			.override_failure_message("jose's player belongs to no scene") \
			.is_true()
	assert_int(Netw.session(jose.tree).scenes.size()) \
			.override_failure_message("jose still holds the level he left") \
			.is_equal(1)
	assert_bool(jose_local.is_visible_in_tree()) \
			.override_failure_message("jose's own body came back invisible") \
			.is_true()
	var jose_level: Node = jose_scene.root
	assert_that(StringName(jose_level.name)) \
			.override_failure_message("jose's player did not return to Level1") \
			.is_equal(&"Level1")
	assert_that(Netw.session(jose.tree).presented_scene.label) \
			.override_failure_message("jose presents no Level1 after returning") \
			.is_equal(&"Level1")

	var maria_on_jose: Node2D = await jose.await_player(&"maria", 2.0)
	var jose_on_maria: Node2D = await maria.await_player(&"jose", 2.0)
	assert_that(maria_on_jose) \
			.override_failure_message("jose does not see maria after return") \
			.is_not_null()
	assert_that(jose_on_maria) \
			.override_failure_message("maria does not see jose after return") \
			.is_not_null()

	var maria_x := maria_on_jose.position.x
	maria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	maria.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(maria_on_jose.position.x) \
			.override_failure_message("jose (returned) stopped seeing maria move") \
			.is_greater(maria_x)

	var jose_local_x := jose_local.position.x
	var jose_remote_x := jose_on_maria.position.x
	jose.simulate_action_press("move_right")
	await game.sync_ticks(16)
	jose.simulate_action_release("move_right")
	await game.sync_ticks(8)
	if OS.get_environment("NETW_RELOCATION_LINK") == "rough":
		await game.sync_ticks(64)
	assert_that(jose_local.position.x) \
			.override_failure_message("jose lost control of his own player") \
			.is_greater(jose_local_x)
	assert_that(jose_on_maria.position.x) \
			.override_failure_message("maria stopped seeing jose move") \
			.is_greater(jose_remote_x)


func test_a_reconnecting_player_is_restored_where_it_saved() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transitions()

	await _teleport_without_bridge(jose, valeria, _LEVEL_2_PATH, &"Level2")
	_assert_arrived_at_marker(jose, "Level2")

	await game.disconnect_runner(jose)
	await game.sync_ticks(16)

	var again := await game.add_client("jose", true)
	await _wait_for_transition(again)
	_assert_arrived_at_marker(again, "Level2")


func test_teleport_to_a_missing_marker_rejects_and_leaves_the_player_home() -> void:
	var valeria := await game.add_host("valeria", true)
	await _wait_for_transition(valeria)

	var tp := await _wait_for_tp(valeria)
	assert_that(tp).is_not_null()
	var before: Vector2 = (valeria.local_player as Node2D).global_position

	var promise := await _wait_for_promise(
		tp.teleport(_LEVEL_2_PATH, ^"%Teleporter/NoSuchMarker")
	)
	assert_bool(promise.is_settled) \
			.override_failure_message("a missing marker never settled the promise") \
			.is_true()
	assert_int(promise.code) \
			.override_failure_message("a missing marker did not reject the teleport") \
			.is_equal(ERR_DOES_NOT_EXIST)

	var player := valeria.local_player as Node2D
	assert_that(player).is_not_null()
	var level: Node = NetwEntity.of(player).scene.root
	assert_that(String(level.name)) \
			.override_failure_message("a rejected teleport moved valeria anyway") \
			.is_equal("Level1")
	assert_float(player.global_position.distance_to(before)).is_less(1.0)


func test_completion_before_local_placement_waits_for_the_marker() -> void:
	await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transitions()

	var layer := _layer_of(jose)
	var tp := await _wait_for_tp(jose)
	assert_that(tp).is_not_null()

	var promise := tp.teleport(_LEVEL_2_PATH, _TP_MARKER)
	layer.answer(OK, "")

	var level: Node = NetwEntity.of(jose.local_player).scene.root
	assert_that(String(level.name)) \
			.override_failure_message("the mover left Level1 before the server moved it") \
			.is_equal("Level1")
	assert_bool(layer.revealing) \
			.override_failure_message("the mover revealed the destination before it arrived") \
			.is_false()
	assert_bool(promise.is_settled) \
			.override_failure_message("a completion settled the teleport before it applied") \
			.is_false()

	await _wait_for_promise(promise)
	assert_int(promise.code).is_equal(OK)
	_assert_arrived_at_marker(jose, "Level2")


func test_a_second_teleport_while_one_is_in_flight_is_one_operation() -> void:
	var valeria := await game.add_host("valeria", true)
	await _wait_for_transition(valeria)

	var layer := _layer_of(valeria)
	var tp := await _wait_for_tp(valeria)
	assert_that(tp).is_not_null()

	var first := tp.teleport(_LEVEL_2_PATH, _TP_MARKER)
	var second := tp.teleport(_LEVEL_1_PATH, _TP_MARKER)

	assert_that(second) \
			.override_failure_message("a second teleport opened a second operation") \
			.is_same(first)
	assert_that(layer.pending) \
			.override_failure_message("a second teleport replaced the pending request") \
			.is_same(first)

	await _wait_for_promise(first)
	assert_int(first.code).is_equal(OK)

	var level: Node = NetwEntity.of(valeria.local_player).scene.root
	assert_that(String(level.name)) \
			.override_failure_message("the overlapping request chose the destination") \
			.is_equal("Level2")


func test_a_mover_leaving_mid_teleport_leaves_the_others_playable() -> void:
	var valeria := await game.add_host("valeria", true)
	var jose := await game.add_client("jose", true)
	await _wait_for_transitions()

	var tp := await _wait_for_tp(jose)
	assert_that(tp).is_not_null()
	tp.teleport(_LEVEL_2_PATH, _TP_MARKER)
	await game.sync_ticks(2)
	await game.disconnect_runner(jose)
	await game.sync_ticks(30)

	var player := valeria.local_player as Node2D
	assert_that(player) \
			.override_failure_message("valeria lost her player when jose left") \
			.is_not_null()
	var level: Node = NetwEntity.of(player).scene.root
	assert_that(String(level.name)).is_equal("Level1")

	var start := player.position.x
	valeria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	valeria.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(player.position.x) \
			.override_failure_message("valeria could not move after jose left mid teleport") \
			.is_greater(start)


func _teleport_without_bridge(
		participant: NetwSceneRunner,
		observer: NetwSceneRunner,
		scene_path: String,
		scene_name: StringName,
) -> void:
	var layer := _layer_of(participant)
	var tp := await _wait_for_tp(participant)
	assert_that(tp).is_not_null()
	participant.simulate_action_press("move_right")
	var promise := tp.teleport(scene_path, _TP_MARKER)
	var moving_frames := 0
	var destination_frames := 0
	var destination_arrived := false
	var input_held := true
	for _i in 300:
		await game.sync_ticks(1)
		if layer.is_moving():
			moving_frames += 1
			var body := participant.local_player as CharacterBody2D
			if body:
				assert_vector(body.velocity).is_equal(Vector2.ZERO)
			if moving_frames == 4:
				participant.simulate_action_release("move_right")
				input_held = false
		var remote := observer.find_player(participant.username) as Node2D
		if remote:
			var scene: NetwSceneHandle = NetwEntity.of(remote).scene
			if scene and StringName(scene.root.name) == scene_name:
				var marker := scene.root.get_node(_TP_MARKER) as Node2D
				var visual := remote.get_node("Icon") as Node2D
				destination_arrived = destination_arrived or (
						remote.global_position.distance_to(marker.global_position) < 1.0
				)
				if destination_arrived:
					destination_frames += 1
					assert_float(visual.global_position.distance_to(marker.global_position)) \
							.override_failure_message(
								"%s rendered %s between teleport endpoints"
								% [observer.username, visual.global_position],
							) \
							.is_less(1.0)
		if promise.is_settled:
			break
	if input_held:
		participant.simulate_action_release("move_right")
	assert_int(moving_frames).is_greater(0)
	assert_int(destination_frames).is_greater(0)
	assert_bool(promise.is_completed) \
			.override_failure_message("teleport to %s never completed" % scene_path) \
			.is_true()


func _wait_for_body_in(
		participant: NetwSceneRunner,
		player_username: StringName,
		label: StringName,
) -> Node2D:
	for _i in 180:
		var body := participant.find_player(player_username) as Node2D
		if body != null and Netw.scene(body).label == label:
			return body
		await game.sync_ticks(1)
	return null


func _wait_for_transition(participant: NetwSceneRunner) -> void:
	var layer := Netw.service(participant.tree, TPLayer) as TPLayer
	while layer and layer.transition_anim.is_playing():
		await game.sync_ticks(1)


func _wait_for_transitions(participants: Array[NetwSceneRunner] = []) -> void:
	var list := participants if not participants.is_empty() else game.runners
	var transitioning := true
	while transitioning:
		transitioning = false
		for participant in list:
			var layer := Netw.service(participant.tree, TPLayer) as TPLayer
			if layer and layer.transition_anim.is_playing():
				transitioning = true
				break
		if transitioning:
			await game.sync_ticks(1)


func _assert_arrived_at_marker(
		participant: NetwSceneRunner,
		level_name: String,
) -> void:
	var player := participant.local_player as Node2D
	assert_that(player) \
			.override_failure_message("%s has no local player to place" % participant.username) \
			.is_not_null()
	var level: Node = NetwEntity.of(player).scene.root
	assert_that(String(level.name)) \
			.override_failure_message("%s is not standing in %s" % [participant.username, level_name]) \
			.is_equal(level_name)
	var marker := level.get_node_or_null(_TP_MARKER) as Node2D
	assert_that(marker) \
			.override_failure_message("%s has no %s to arrive on" % [level_name, _TP_MARKER]) \
			.is_not_null()
	assert_float(player.global_position.distance_to(marker.global_position)) \
			.override_failure_message(
				"%s arrived at %s instead of %s's marker at %s"
				% [
					participant.username,
					player.global_position,
					level_name,
					marker.global_position,
				],
			) \
			.is_less(1.0)


func _tp_of(participant: NetwSceneRunner) -> TPComponent:
	var player := participant.local_player
	if not is_instance_valid(player):
		return null
	return player.get_node_or_null("%TPComponent") as TPComponent


func _layer_of(participant: NetwSceneRunner) -> TPLayer:
	return Netw.service(participant.tree, TPLayer) as TPLayer


func _wait_for_tp(participant: NetwSceneRunner) -> TPComponent:
	var layer := _layer_of(participant)
	for _i in 240:
		var tp := _tp_of(participant)
		if tp and not layer.is_moving():
			return tp
		await game.sync_ticks(1)
	return null


func _wait_for_promise(promise: NetwPromise) -> NetwPromise:
	for _i in 300:
		if promise.is_settled:
			break
		await game.sync_ticks(1)
	return promise


func _request_level_2(participant: NetwSceneRunner) -> NetwPromise:
	var promise := Netw.session(participant.tree).request_scene(_LEVEL_2_PATH)
	for _i in 180:
		if promise.is_settled:
			break
		await game.sync_ticks(1)
	assert_that(promise.is_settled).is_true()
	return promise
