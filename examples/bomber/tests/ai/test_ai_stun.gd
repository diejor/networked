# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_hunter_stuns_target_replicated_to_all_peers() -> void:
	var runners := await add_players_and_start(3)

	# valeria hunts jose. lucia wanders. jose has no AI (stays idle).
	var hunter := BomberAI.create(runners[0], &"valeria")
	hunter.goal = BomberAI.Goal.hunt(&"jose")
	var wanderer := BomberAI.create(runners[2], &"lucia")
	wanderer.goal = BomberAI.Goal.wander()

	var ais: Array[BomberAI] = [hunter, wanderer]
	await run_until(
		ais,
		500,
		func() -> bool:
			return is_stunned(runners[0], &"jose")
	)

	# Stun visible on every peer's view of jose.
	for r in runners:
		assert_bool(is_stunned(r, &"jose")).is_true()


func test_mutual_hunters_first_stun_wins() -> void:
	var runners := await add_players_and_start(2)

	var ai_v := BomberAI.create(runners[0], &"valeria")
	var ai_j := BomberAI.create(runners[1], &"jose")
	ai_v.goal = BomberAI.Goal.hunt(&"jose")
	ai_j.goal = BomberAI.Goal.hunt(&"valeria")

	var ais: Array[BomberAI] = [ai_v, ai_j]
	await run_until(
		ais,
		500,
		func() -> bool:
			return is_stunned(runners[0], &"jose") \
					or is_stunned(runners[0], &"valeria")
	)

	# At least one player is stunned.
	var v_stunned := is_stunned(runners[0], &"valeria")
	var j_stunned := is_stunned(runners[0], &"jose")
	assert_bool(v_stunned or j_stunned).is_true()

	# Both peers agree on who is stunned.
	assert_bool(is_stunned(runners[1], &"valeria")).is_equal(v_stunned)
	assert_bool(is_stunned(runners[1], &"jose")).is_equal(j_stunned)
