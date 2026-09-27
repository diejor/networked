extends BomberAiSuite

func test_full_match_under_rough_link() -> void:
	var runners := await add_players_and_start(2)

	game.degrade(runners[1]).profile(NetwLink.Profile.MOBILE_4G)

	var ais := make_ais(runners, BomberAI.Goal.score())
	var rocks_before := rocks_left(game.host)
	await run_until(
		ais,
		game.seconds_to_ticks(27.0),
		func() -> bool: return rocks_left(game.host) < rocks_before,
	)

	assert_int(rocks_left(game.host)).is_less(rocks_before)

	await settle_network(runners)
	var converged := await tick_until(
		func() -> bool:
			return rocks_left(runners[1]) == rocks_left(game.host),
	)
	assert_bool(converged).is_true()


func test_positions_converge_after_ai_stops_on_rough_link() -> void:
	var runners := await add_players_and_start(3)

	for i in range(1, runners.size()):
		game.degrade(runners[i]).profile(NetwLink.Profile.MOBILE_4G)

	var ais := make_ais(runners, BomberAI.Goal.wander())

	await run_until(ais, game.seconds_to_ticks(10.0))

	for ai in ais:
		ai.goal = BomberAI.Goal.idle()
	await settle_network(runners, 60)

	for r in runners:
		var p := game.host.find_player(r.username) as Node2D
		if is_instance_valid(p):
			p.position += Vector2(0.1, 0.1)

	var converged := await tick_until(
		func() -> bool:
			return _views_converged(runners, 8.0),
	)
	assert_bool(converged).is_true()


func _views_converged(runners: Array[NetwSceneRunner], epsilon: float) -> bool:
	for r in runners:
		for other in runners:
			if r == other:
				continue
			var _name := StringName(other.username)
			var host_view := game.host.find_player(_name) as Node2D
			var peer_view := r.find_player(_name) as Node2D
			if not is_instance_valid(host_view) \
					or not is_instance_valid(peer_view):
				return false
			if absf(peer_view.position.x - host_view.position.x) > epsilon:
				return false
			if absf(peer_view.position.y - host_view.position.y) > epsilon:
				return false
	return true
