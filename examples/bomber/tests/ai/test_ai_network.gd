# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

func test_full_match_under_rough_link() -> void:
	var runners := await add_players_and_start(2)

	game.degrade(runners[1]).profile(NetwLink.Profile.MOBILE_4G)

	var ais := make_ais(runners, BomberAI.Goal.score())
	var rocks_before := rocks_left(game.host)
	# Reliable bomb spawns punch through the lossy link, so the AIs clear the
	# board. Exit as soon as a rock falls instead of grinding a fixed budget; the
	# cap is generous game time for the first clear under the degraded link.
	await run_until(
		ais,
		game.seconds_to_ticks(27.0),
		func() -> bool: return rocks_left(game.host) < rocks_before,
	)

	# The AIs made progress clearing the board.
	assert_int(rocks_left(game.host)).is_less(rocks_before)

	# Once the link clears, the board replication converges across peers.
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

	# Active phase: AIs wander under rough link long enough to diverge the
	# unreliable position streams across peers before they stop.
	await run_until(ais, game.seconds_to_ticks(10.0))

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

	# The unreliable position stream self-heals once motion stops, so every
	# peer's view of every player converges to the authoritative value.
	var converged := await tick_until(
		func() -> bool:
			return _views_converged(runners, 8.0),
	)
	assert_bool(converged).is_true()


# True when every peer's view of every other player matches the host within
# [param epsilon]. Self views are skipped since the controlling client has no
# local reconciliation against itself.
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
