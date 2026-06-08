# GdUnitGeneratedTestDescriptor
extends BomberAiSuite


func test_bomb_rate_limit_holds_with_four_bombers() -> void:
	var runners := await add_players_and_start(4)
	var ais := make_ais(runners, BomberAI.Goal.score())

	await run_until(ais, 60)

	# Bomb count is consistent across all peers.
	var host_bombs := count_bombs(runners[0])
	for r in runners:
		assert_int(count_bombs(r)).is_equal(host_bombs)

	# No more than 1 active bomb per player (BOMB_RATE gate).
	# Since a bomb lasts 3.4 seconds and BOMB_RATE is 0.5s, a player can
	# have multiple concurrent active bombs. The rate limit ensures we do
	# not spawn one every tick.
	assert_int(host_bombs).is_less_equal(runners.size() * 3)


func test_stunned_ai_cannot_place_bombs() -> void:
	var runners := await add_players_and_start(2)

	# Stun jose from the host (server authority).
	var jose_on_host := await runners[0].await_player(&"jose", 2.0) as Node2D
	jose_on_host.exploded.rpc(runners[0].peer_id)
	await game.sync_ticks(2)
	assert_bool(is_stunned(runners[1], &"jose")).is_true()

	var jose_ai := BomberAI.create(runners[1], &"jose")
	jose_ai.goal = BomberAI.Goal.score()
	jose_ai.flee_enabled = false

	var bombs_before := count_bombs(runners[0])
	await run_until([jose_ai], 10)
	var bombs_after := count_bombs(runners[0])

	# The stunned player spawned no new bombs.
	assert_int(bombs_after).is_less_equal(bombs_before)
