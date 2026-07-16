class_name TestQuickStartGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")
const PLAYER := preload("res://examples/quick_start/Player.tscn")
const LEVEL_1_SPAWN := (
		"uid://bqi7mvxdnvgch::Player"
)
const DATABASE := preload("res://examples/quick_start/quick_start_database.tres")
const _LEVEL_1_UID := "uid://bqi7mvxdnvgch"
const _LEVEL_2_UID := "uid://cthf7wjwb4n77"

var game: NetwGameHarness


func before() -> void:
	# The player archetype persists through this shared database resource, which
	# points at the repo-local res://examples/quick_start/saves. Redirect it to a
	# gdUnit temp dir so a persisted player state never leaks
	# into the next run's spawns.
	var fs := DATABASE.backend as FileSystemDatabase
	fs.base_dir = create_temp_dir("quick_start_saves")


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


func test_host_input_reaches_local_player_after_add_host() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	await game.wait_for_transition(valeria)

	var player := valeria.local_player as Node2D
	assert_that(player).is_not_null()
	var start := player.position.x

	valeria.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	valeria.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_client_input_reaches_local_player() -> void:
	await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	await game.wait_for_transition(jose)

	var player := jose.local_player as Node2D
	assert_that(player).is_not_null()
	var input := _input_for(player)
	var start := player.position.x

	await game.sync_ticks(16)
	jose.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(input.state[&"move_right"]).is_true()
	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	jose.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(input.state[&"move_right"]).is_false()
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_host_input_replicates_to_client() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	await game.wait_for_transitions()

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
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	await game.wait_for_transitions()

	var valeria_on_jose: Node2D = await jose.await_player(&"valeria", 2.0)
	var start := valeria_on_jose.position.x

	# Apply a rough link with 50% packet loss and delay
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
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())

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

	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_true()
	assert_that(display.has_slot(jose.slot)).is_true()


func test_host_scene_request_keeps_camera_current() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	await game.wait_for_transition(valeria)

	var player := valeria.local_player as Node2D
	var camera := player.get_node("Camera2D") as Camera2D
	assert_that(player).is_not_null()
	assert_that(camera).is_not_null()
	assert_that(player.get_viewport().get_camera_2d()).is_equal(camera)

	var promise := await _request_level_2(valeria)
	var level_2 := await valeria.await_scene(&"Level2", 2.0)

	assert_that(promise.is_completed).is_true()
	assert_that(promise.result).is_equal(NetwScenePromise.Result.OK)
	assert_that(level_2).is_not_null()
	assert_that(player.get_viewport().get_camera_2d()).is_equal(camera)


func test_clients_still_see_each_other_after_scene_change() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	var maria := await game.add_client("maria", true, _level_1_spawn())
	await game.wait_for_transitions()

	# Baseline: in the shared start scene, jose already sees maria move. This is
	# the "same level works" half of the report, and guards against the repro
	# failing for an unrelated reason.
	var maria_on_jose_start: Node2D = await jose.await_player(&"maria", 2.0)
	assert_that(maria_on_jose_start).is_not_null()
	var base_x := maria_on_jose_start.position.x
	maria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	maria.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(maria_on_jose_start.position.x).is_greater(base_x)

	# quick_start is CONCURRENT for its per-player teleporter, so each player
	# requests Level2 for themselves. Once all three arrive they co-locate and
	# see each other again in the destination scene.
	for requester in [valeria, jose, maria]:
		var promise := await _request_level_2(requester)
		assert_that(promise.result).is_equal(NetwScenePromise.Result.OK)
	for runner in [valeria, jose, maria]:
		assert_that(await runner.await_scene(&"Level2", 2.0)).is_not_null()

	# After replacement, maria must move in her view and both remote views.
	var maria_local := maria.local_player as Node2D
	var maria_on_valeria: Node2D = await valeria.await_player(&"maria", 2.0)
	var maria_on_jose: Node2D = await jose.await_player(&"maria", 2.0)
	assert_that(maria_on_jose).is_not_null()

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
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	var maria := await game.add_client("maria", true, _level_1_spawn())
	await game.wait_for_transitions()

	# jose teleports to Level2 and confirms he actually left Level1.
	await _teleport(jose, _LEVEL_2_UID)
	assert_that(await jose.await_scene(&"Level2", 2.0)) \
			.override_failure_message("jose never reached Level2") \
			.is_not_null()

	# jose teleports back to Level1, the direction the report says breaks.
	await _teleport(jose, _LEVEL_1_UID)
	await game.sync_ticks(20)

	# jose's own player must be enrolled under the shared Level1 wrapper on his
	# own tree, not stranded in the old Level2 subtree.
	var jose_local := jose.local_player as Node2D
	assert_that(jose_local) \
			.override_failure_message("jose has no local player after return") \
			.is_not_null()
	var jose_scene := MultiplayerScene.of(jose_local)
	assert_that(jose_scene) \
			.override_failure_message("jose's player belongs to no scene") \
			.is_not_null()
	assert_that(StringName(jose_scene.level.name)) \
			.override_failure_message("jose's player did not return to Level1") \
			.is_equal(&"Level1")

	# The returning client and the resident client must see each other again.
	var maria_on_jose: Node2D = await jose.await_player(&"maria", 2.0)
	var jose_on_maria: Node2D = await maria.await_player(&"jose", 2.0)
	assert_that(maria_on_jose) \
			.override_failure_message("jose does not see maria after return") \
			.is_not_null()
	assert_that(jose_on_maria) \
			.override_failure_message("maria does not see jose after return") \
			.is_not_null()

	# maria (a Level1 resident) must relay to the returned jose.
	var maria_x := maria_on_jose.position.x
	maria.simulate_action_press("move_right")
	await game.sync_ticks(16)
	maria.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(maria_on_jose.position.x) \
			.override_failure_message("jose (returned) stopped seeing maria move") \
			.is_greater(maria_x)

	# jose must still drive his own player and be seen doing so.
	var jose_local_x := jose_local.position.x
	var jose_remote_x := jose_on_maria.position.x
	jose.simulate_action_press("move_right")
	await game.sync_ticks(16)
	jose.simulate_action_release("move_right")
	await game.sync_ticks(8)
	assert_that(jose_local.position.x) \
			.override_failure_message("jose lost control of his own player") \
			.is_greater(jose_local_x)
	assert_that(jose_on_maria.position.x) \
			.override_failure_message("maria stopped seeing jose move") \
			.is_greater(jose_remote_x)


func _teleport(participant: NetwSceneRunner, scene_uid: String) -> void:
	var player := participant.local_player
	var tp := player.get_node("%TPComponent") as TPComponent
	# The player walks between teleporters seconds apart, well past the settle
	# window. Wait it out so the request is a real teleport, not an ignored one.
	for _i in 240:
		if not tp.is_settling() and not tp._tp_mutex.is_locked():
			break
		await game.sync_ticks(1)
	var promise := tp.teleport(_tp_target(scene_uid))
	for _i in 300:
		if promise.is_completed:
			break
		await game.sync_ticks(1)
	assert_that(promise.is_completed) \
			.override_failure_message("teleport to %s never completed" % scene_uid) \
			.is_true()


func _tp_target(scene_uid: String) -> SceneNodePath:
	var target := SceneNodePath.new()
	target.scene_path = scene_uid
	target.node_path = "%Teleporter/Marker2D"
	return target


func _request_level_2(participant: NetwSceneRunner) -> NetwScenePromise:
	var promise := participant.tree.api.scenes.request_change(&"Level2")
	for _i in 180:
		if promise.is_completed:
			break
		await game.sync_ticks(1)
	assert_that(promise.is_completed).is_true()
	return promise


func _input_for(player: Node) -> MoveInputComponent:
	return player.get_node("%InputComponent") as MoveInputComponent


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)
