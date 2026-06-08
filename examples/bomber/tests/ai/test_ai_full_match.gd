# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_two_ais_play_to_winner() -> void:
	game.set_time_factor(20.)
	var runners := await add_players_and_start(2)

	var ais := make_ais(runners, BomberAI.Goal.score())

	await run_until(
		ais,
		2000,
		func() -> bool:
			return winner_visible(runners[0])
	)

	# The winner banner is visible on both peers.
	assert_bool(winner_visible(runners[0])).is_true()
	assert_bool(winner_visible(runners[1])).is_true()

	# Both peers show the same winner name.
	var w0 := _get_world(runners[0]).level.get_node("Winner") as Label
	var w1 := _get_world(runners[1]).level.get_node("Winner") as Label
	assert_str(w0.text).is_equal(w1.text)


func test_four_ais_play_full_match() -> void:
	game.set_time_factor(20.)
	var runners := await add_players_and_start(4)

	var ais := make_ais(runners, BomberAI.Goal.score())

	await run_until(
		ais,
		3000,
		func() -> bool:
			return rocks_left(runners[0]) == 0
	)

	assert_int(rocks_left(runners[0])).is_equal(0)

	# All four peers agree on every player's score.
	for r in runners:
		for other in runners:
			var s0 := get_score(runners[0], other.peer_id)
			var s_r := get_score(r, other.peer_id)
			assert_int(s_r).is_equal(s0)
