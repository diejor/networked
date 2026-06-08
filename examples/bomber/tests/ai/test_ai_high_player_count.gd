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

	await run_until(ais, 600)

	# Every player node still exists on every peer.
	for r in runners:
		for other in runners:
			var name := StringName(other.username)
			assert_bool(r.find_player(name) != null).is_true()


func test_four_scorers_final_scores_consistent() -> void:
	var runners := await add_players_and_start(4)

	var ais: Array[BomberAI] = []
	for r in runners:
		var ai := BomberAI.create(r, StringName(r.username))
		ai.goal = BomberAI.Goal.score()
		ais.append(ai)

	await run_until(ais, 400)

	# Every peer agrees on every player's score.
	for r in runners:
		for other in runners:
			var s0 := get_score(runners[0], other.peer_id)
			var s_r := get_score(r, other.peer_id)
			assert_int(s_r).is_equal(s0)

	# At least one player scored.
	var total := 0
	for r in runners:
		total += get_score(runners[0], r.peer_id)
	assert_int(total).is_greater(0)
