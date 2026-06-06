class_name TestDailyGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/daily/Main.tscn")
const LEVEL_1 := preload("res://examples/daily/Level1.tscn")
const LEVEL_2 := preload("res://examples/daily/Level2.tscn")
const PLAYER := preload("res://examples/daily/Player.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_host_input_reaches_local_player_after_add_host() -> void:
	var alice := await game.add_host("alice")

	var player := alice.local_player as Node2D
	assert_that(player).is_not_null()
	var start := player.position.x

	alice.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	alice.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_client_input_reaches_local_player() -> void:
	await game.add_host("alice")
	var bob := await game.add_client("bob")

	var player := bob.local_player as Node2D
	assert_that(player).is_not_null()
	var input := _input_for(player)
	var start := player.position.x

	bob.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(input.state[&"move_right"]).is_true()
	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	bob.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(input.state[&"move_right"]).is_false()
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func test_host_input_replicates_to_client() -> void:
	var alice := await game.add_host("alice")
	var bob := await game.add_client("bob")

	var alice_on_bob: Node2D = await bob.await_player(&"alice", 2.0)
	assert_that(alice_on_bob).is_not_null()
	var start := alice_on_bob.position.x

	alice.simulate_action_press("move_right")
	await game.sync_ticks(16)
	alice.simulate_action_release("move_right")
	await game.sync_ticks(16)

	assert_that(alice_on_bob.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()


func _input_for(player: Node) -> MoveInputComponent:
	return player.get_node("%InputComponent") as MoveInputComponent
