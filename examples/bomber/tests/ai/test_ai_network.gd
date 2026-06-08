# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_full_match_under_rough_link() -> void:
	var runners := await add_players_and_start(2)

	game.link(runners[1], runners[0]) \
			.latency_ms(600) \
			.jitter_ms(200) \
			.loss(0.05)
	
	game.link(runners[0], runners[1]) \
			.latency_ms(600) \
			.jitter_ms(200) \
			.loss(0.05)

	var ais := make_ais(runners, BomberAI.Goal.score())
	await run_until(
		ais,
		3000,
		func() -> bool:
			return winner_visible(runners[0])
	)

	await settle_network(runners)

	assert_bool(winner_visible(runners[0])).is_true()
	assert_bool(winner_visible(runners[1])).is_true()


func test_positions_converge_after_ai_stops_on_rough_link() -> void:
	var runners := await add_players_and_start(3)

	for i in range(1, runners.size()):
		game.link(runners[i], runners[0]) \
			.latency_ms(600) \
			.jitter_ms(200) \
			.loss(0.05)

	var ais := make_ais(runners, BomberAI.Goal.wander())

	# Active phase: AIs wander under rough link.
	await run_until(ais, 200)

	# Stop all AIs and let the network settle.
	for ai in ais:
		ai.goal = BomberAI.Goal.idle()
	await run_until(ais, 60)

	# Every peer's view of every player converges.
	for r in runners:
		for other in runners:
			var host_view := runners[0].find_player(
				StringName(other.username),
			) as Node2D
			var peer_view := r.find_player(
				StringName(other.username),
			) as Node2D
			assert_float(peer_view.position.x).is_equal_approx(
				host_view.position.x,
				8.0,
			)
			assert_float(peer_view.position.y).is_equal_approx(
				host_view.position.y,
				8.0,
			)


func test_four_ais_progressive_link_degradation() -> void:
	var runners := await add_players_and_start(4)

	var ais := make_ais(runners, BomberAI.Goal.score())

	var stages := [0.1, 0.3, 0.5]
	for loss in stages:
		for i in range(1, runners.size()):
			game.link(runners[i], runners[0]).exact() \
					.loss_prob(loss) \
					.delay_polls(int(loss * 10)) \
					.seed(1)

		await run_until(ais, 100)

	await settle_network(runners)

	# Scores still consistent after three degradation stages.
	for r in runners:
		for other in runners:
			var s0 := get_score(runners[0], other.peer_id)
			var s_r := get_score(r, other.peer_id)
			assert_int(s_r).is_equal(s0)
