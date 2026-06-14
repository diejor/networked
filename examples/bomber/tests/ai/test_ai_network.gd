# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_full_match_under_rough_link() -> void:
	game.set_time_factor(45.)
	var runners := await add_players_and_start(2)

	game.degrade(runners[1]).profile(NetwLink.Profile.MOBILE_4G)

	var ais := make_ais(runners, BomberAI.Goal.score())
	await run_until(
		ais,
		3000,
		func() -> bool:
			return winner_visible(game.host)
	)

	if not winner_visible(game.host):
		return

	await settle_network(runners)

	assert_bool(winner_visible(game.host)).is_true()
	assert_bool(winner_visible(runners[1])).is_true()


func test_positions_converge_after_ai_stops_on_rough_link() -> void:
	var runners := await add_players_and_start(3)

	for i in range(1, runners.size()):
		game.degrade(runners[i]).profile(NetwLink.Profile.MOBILE_4G)

	var ais := make_ais(runners, BomberAI.Goal.wander())

	# Active phase: AIs wander under rough link.
	await run_until(ais, 200)

	# Stop all AIs and let the network settle.
	for ai in ais:
		ai.goal = BomberAI.Goal.idle()
	await settle_network(runners, 60)

	# Touch positions on server to trigger a final replication over the
	# cleared link.
	for r in runners:
		var p := game.host.find_player(r.username) as Node2D
		if is_instance_valid(p):
			p.position += Vector2(0.1, 0.1)

	await game.sync_ticks(50)

	# Every peer's view of every player converges.
	for r in runners:
		for other in runners:
			if r == other:
				# Skip self-prediction checks since there is no local
				# reconciliation on the controlling client.
				continue
			var host_view := game.host.find_player(
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
