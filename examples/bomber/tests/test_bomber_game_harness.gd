class_name TestBomberGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_host_input_reaches_local_player_after_begin_game() -> void:
	game.show_views()
	var alice := await game.add_host("alice", false)

	var gamestate := alice.tree.get_service(BomberGamestate) as BomberGamestate
	assert_that(gamestate).is_not_null()
	gamestate.begin_game()

	await alice.await_scene(&"World", 2.0)
	var player := await alice.await_player(&"alice", 2.0) as Node2D
	assert_that(alice.local_player).is_equal(player)

	var input := player.get_node("Inputs")
	var start := player.position.x

	await game.sync_ticks(16)
	alice.simulate_action_press("move_right")
	await game.sync_ticks(8)

	assert_that(input.state[&"move_right"]).is_true()
	assert_that(player.position.x).is_greater(start)
	assert_that(Input.is_action_pressed(&"move_right")).is_false()

	alice.simulate_action_release("move_right")
	await game.sync_ticks(2)

	assert_that(input.state[&"move_right"]).is_false()
	assert_that(Input.is_action_pressed(&"move_right")).is_false()
