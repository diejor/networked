class_name TestQuickStartGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")
const PLAYER := preload("res://examples/quick_start/Player.tscn")
const LEVEL_1_SPAWN := (
		"uid://bqi7mvxdnvgch::Player/%MultiplayerEntity"
)

var game: NetwGameHarness


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


func _input_for(player: Node) -> MoveInputComponent:
	return player.get_node("%InputComponent") as MoveInputComponent


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)
