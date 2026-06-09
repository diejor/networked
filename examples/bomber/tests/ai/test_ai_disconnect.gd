# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_active_ai_disconnects_mid_match() -> void:
	var runners := await add_players_and_start(3)
	var ais := make_ais(runners, BomberAI.Goal.score())

	# Let AIs play for a bit to generate real traffic.
	await run_until(ais, 80)

	var gamestate := runners[0].tree.get_service(BomberGamestate) \
			as BomberGamestate
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(
		func(_what: String) -> void: errored[0] = true,
	)

	# Disconnect the second client while AIs are active.
	await game.disconnect_runner(runners[2])
	await game.sync_ticks(4)

	assert_bool(errored[0]).is_true()
	assert_bool(gamestate.world == null).is_true()


func test_half_lobby_disconnects_during_active_play() -> void:
	var runners := await add_players_and_start(4)
	var ais := make_ais(runners, BomberAI.Goal.random())

	await run_until(ais, 60)

	var gamestate := runners[0].tree.get_service(BomberGamestate) \
			as BomberGamestate
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(
		func(_what: String) -> void: errored[0] = true,
	)

	await game.disconnect_runner(runners[2])
	await game.disconnect_runner(runners[3])
	await game.sync_ticks(4)

	assert_bool(errored[0]).is_true()
	assert_bool(gamestate.world == null).is_true()
