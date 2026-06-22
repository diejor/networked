# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_eight_players_mixed_goals_no_crash() -> void:
	var runners := await add_players_and_start(8)

	var goals := [
		BomberAI.Goal.score(),
		BomberAI.Goal.hunt(&"valeria"),
		BomberAI.Goal.wander(),
		BomberAI.Goal.score(),
		BomberAI.Goal.random(),
		BomberAI.Goal.wander(),
		BomberAI.Goal.hunt(&"carlos"),
		BomberAI.Goal.score(),
	]

	var ais: Array[BomberAI] = []
	for i in runners.size():
		var ai := BomberAI.create(
			runners[i],
			StringName(runners[i].username),
		)
		ai.goal = goals[i]
		ais.append(ai)

	# A no-crash smoke needs a representative slice of mixed activity, not forty
	# game-seconds. Eight game-seconds spans several bomb cycles at any tickrate.
	await run_until(ais, game.seconds_to_ticks(8.0))

	# Every player node still exists on every peer.
	for r in runners:
		for other in runners:
			var _name := StringName(other.username)
			assert_bool(r.find_player(_name) != null).is_true()


func test_four_scorers_final_scores_consistent() -> void:
	var runners := await add_players_and_start(4)

	var ais: Array[BomberAI] = []
	for r in runners:
		var ai := BomberAI.create(r, StringName(r.username))
		ai.goal = BomberAI.Goal.score()
		ais.append(ai)

	# Exit as soon as a score has landed and every peer agrees, rather than
	# grinding a fixed budget. The cap is generous game time for the first bomb
	# cycle to score and replicate; reaching it means the scenario never settled.
	var scored_and_consistent := func() -> bool:
		var host_total := 0
		for r in runners:
			host_total += get_score(game.host, r.peer_id)
		if host_total <= 0:
			return false
		for r in runners:
			for other in runners:
				if get_score(r, other.peer_id) \
						!= get_score(game.host, other.peer_id):
					return false
		return true
	var settled := await run_until(
		ais,
		game.seconds_to_ticks(20.0),
		scored_and_consistent,
	)
	assert_int(settled).is_less(game.seconds_to_ticks(20.0))

	# Every peer agrees on every player's score.
	for r in runners:
		for other in runners:
			var s0 := get_score(game.host, other.peer_id)
			var s_r := get_score(r, other.peer_id)
			assert_int(s_r).is_equal(s0)

	# At least one player scored.
	var total := 0
	for r in runners:
		total += get_score(game.host, r.peer_id)
	assert_int(total).is_greater(0)
