extends BomberAiSuite

func test_active_ai_disconnects_mid_match() -> void:
	var runners := await add_players_and_start(3)
	var ais := make_ais(runners, BomberAI.Goal.score())

	await run_until(ais, 80)

	var gamestate := Netw.service(game.host.tree, BomberGamestate) \
			as BomberGamestate
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(
		func(_what: String) -> void: errored[0] = true,
	)

	var gone := StringName(runners[2].username)
	var survivors: Array[NetwSceneRunner] = [runners[0], runners[1]]

	await game.disconnect_runner(runners[2])

	var dropped := await tick_until(
		func() -> bool:
			for r: NetwSceneRunner in survivors:
				if r.find_player(gone) != null:
					return false
			return true,
		60,
	)
	assert_bool(dropped).is_true()

	assert_bool(errored[0]).is_false()
	assert_bool(gamestate.world != null).is_true()

	for r: NetwSceneRunner in survivors:
		for s: NetwSceneRunner in survivors:
			assert_bool(r.find_player(StringName(s.username)) != null).is_true()

	await drain_frames(get_tree(), 10)


func test_half_lobby_disconnects_during_active_play() -> void:
	var runners := await add_players_and_start(4)
	var ais := make_ais(runners, BomberAI.Goal.random())

	await run_until(ais, 60)

	var gamestate := Netw.service(game.host.tree, BomberGamestate) \
			as BomberGamestate
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(
		func(_what: String) -> void: errored[0] = true,
	)

	var gone: Array[StringName] = [
		StringName(runners[2].username),
		StringName(runners[3].username),
	]
	var survivors: Array[NetwSceneRunner] = [runners[0], runners[1]]

	await game.disconnect_runner(runners[2])
	await game.disconnect_runner(runners[3])

	var dropped := await tick_until(
		func() -> bool:
			for r: NetwSceneRunner in survivors:
				for g: StringName in gone:
					if r.find_player(g) != null:
						return false
			return true,
		60,
	)
	assert_bool(dropped).is_true()

	assert_bool(errored[0]).is_false()
	assert_bool(gamestate.world != null).is_true()
	for r: NetwSceneRunner in survivors:
		for s: NetwSceneRunner in survivors:
			assert_bool(r.find_player(StringName(s.username)) != null).is_true()

	await drain_frames(get_tree(), 10)
