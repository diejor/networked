# GdUnitGeneratedTestDescriptor
extends BomberAiSuite


func test_soak_500_ticks_random_goals() -> void:
	var runners := await add_players_and_start(2)
	var ais := make_ais(runners, BomberAI.Goal.random())

	await run_until(ais, 500)

	for r in runners:
		for other in runners:
			var name := StringName(other.username)
			assert_bool(r.find_player(name) != null).is_true()


func test_soak_four_players_rough_link_300_ticks() -> void:
	var runners := await add_players_and_start(4)

	var conditions := NetwLinkConditions.new(1)
	conditions.loss_probability = 0.3
	conditions.delay_polls = 3
	for i in range(1, runners.size()):
		game.set_link_conditions(runners[i], conditions, runners[0])

	var ais := make_ais(runners, BomberAI.Goal.random())
	await run_until(ais, 300)

	for r in runners:
		for other in runners:
			var name := StringName(other.username)
			assert_bool(r.find_player(name) != null).is_true()


func test_goal_switching_mid_match() -> void:
	var runners := await add_players_and_start(2)

	var ai := BomberAI.create(runners[0], &"valeria")
	var idle := BomberAI.create(runners[1], &"jose")
	idle.goal = BomberAI.Goal.wander()

	var phases: Array[BomberAI.Goal] = [
		BomberAI.Goal.score(),
		BomberAI.Goal.hunt(&"jose"),
		BomberAI.Goal.wander(),
		BomberAI.Goal.idle(),
		BomberAI.Goal.score(),
	]

	for phase_goal in phases:
		ai.goal = phase_goal
		await run_until([ai, idle], 60)

	# Survived all transitions without crashing.
	assert_bool(runners[0].find_player(&"valeria") != null).is_true()
