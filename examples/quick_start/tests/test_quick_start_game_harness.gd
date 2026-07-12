class_name TestQuickStartGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")
const PLAYER := preload("res://examples/quick_start/Player.tscn")
const LEVEL_1_SPAWN := (
		"uid://bqi7mvxdnvgch::Player"
)
const LEVEL_2_TARGET := "uid://cthf7wjwb4n77::%Teleporter/Marker2D"
const DATABASE := preload("res://examples/quick_start/quick_start_database.tres")

var game: NetwGameHarness


func before() -> void:
	# The player archetype persists through this shared database resource, which
	# points at the repo-local res://examples/quick_start/saves. Redirect it to a
	# gdUnit temp dir so the teleport test's persisted Level2 state never leaks
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


func test_show_views_displays_each_participant() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	var host_view := valeria.tree.get_service(HostSceneView) as HostSceneView
	if not host_view:
		host_view = valeria.tree.find_service_node(HostSceneView) as HostSceneView

	var display := game.show_views()
	await get_tree().process_frame

	assert_that(host_view).is_not_null()
	assert_that(display.has_slot(valeria.slot)).is_true()
	assert_that(display.has_slot(jose.slot)).is_true()
	assert_that(valeria.slot.visible).is_true()
	assert_that(jose.slot.visible).is_true()
	assert_that(host_view.visible).is_true()

	display.remove_slot(valeria.slot)
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_false()
	assert_that(valeria.slot.visible).is_false()
	assert_that(host_view.visible).is_true()


func test_show_views_can_be_called_before_adding_participants() -> void:
	var display := game.show_views()

	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	var jose := await game.add_client("jose", true, _level_1_spawn())
	await get_tree().process_frame

	assert_that(display.has_slot(valeria.slot)).is_true()
	assert_that(display.has_slot(jose.slot)).is_true()


func test_host_teleport_keeps_camera_current_and_snaps() -> void:
	var valeria := await game.add_host("valeria", true, _level_1_spawn())
	await game.wait_for_transition(valeria)

	var player := valeria.local_player as Node2D
	var camera := player.get_node("Camera2D") as Camera2D
	assert_that(player).is_not_null()
	assert_that(camera).is_not_null()
	assert_that(player.get_viewport().get_camera_2d()).is_equal(camera)

	var tp := player.get_node("%TPComponent") as TPComponent
	var promise := tp.teleport(SceneNodePath.new(LEVEL_2_TARGET))
	for _i in 180:
		if promise.is_completed:
			break
		await game.sync_ticks(1)
	await game.wait_for_transition(valeria)

	assert_that(promise.is_completed).is_true()
	assert_that(player.global_position).is_equal(Vector2(379, 223))
	assert_that(player.get_viewport().get_camera_2d()).is_equal(camera)


func test_clients_still_see_each_other_after_cross_scene_teleport() -> void:
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

	# Both clients cross into Level2 (the mover half of the report).
	await _teleport_to_level_2(jose)
	await _teleport_to_level_2(maria)

	# After the cross-scene move, split the three views apart: maria must move,
	# the host must see it, and jose (another client) must see it too. The
	# regression this guards: the teleport clamped the mover's synchronizers to
	# server-only and never restored per-client visibility, so only the host saw
	# the mover after any scene change while the clients froze at the arrival
	# spot.
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


func _teleport_to_level_2(participant) -> void:
	var player := participant.local_player as Node
	var tp := player.get_node("%TPComponent") as TPComponent
	var promise := tp.teleport(SceneNodePath.new(LEVEL_2_TARGET))
	for _i in 180:
		if promise.is_completed:
			break
		await game.sync_ticks(1)
	await game.wait_for_transition(participant)
	assert_that(promise.is_completed).is_true()


func _input_for(player: Node) -> MoveInputComponent:
	return player.get_node("%InputComponent") as MoveInputComponent


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)
