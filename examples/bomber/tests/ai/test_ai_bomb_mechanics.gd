# GdUnitGeneratedTestDescriptor
extends BomberAiSuite

const PLAYER_SCRIPT := preload("res://examples/bomber/game/player.gd")


func test_bomb_rate_limit_holds_with_four_bombers() -> void:
	var runners := await add_players_and_start(4)
	var ais := make_ais(runners, BomberAI.Goal.score())

	var ticks := 60
	await run_until(ais, ticks)
	var settled := await tick_until(
		func() -> bool:
			var _host_bombs := count_bombs(game.host)
			for r in runners:
				if count_bombs(r) != _host_bombs:
					return false
			return true,
		60,
	)
	assert_bool(settled).is_true()

	# Bomb count is consistent across all peers.
	var host_bombs := count_bombs(game.host)
	for r in runners:
		assert_int(count_bombs(r)).is_equal(host_bombs)

	# Each player starts off cooldown, then can spawn once per BOMB_RATE.
	# The rate limit keeps the total far below one bomb per AI tick.
	var clock := game.host.tree.get_service(MultiplayerClock) as MultiplayerClock
	var tickrate := clock.tickrate if clock else NetwGameHarness.DEFAULT_TICKRATE
	var seconds := float(ticks) / float(tickrate)
	var max_bombs_per_player := 1 + ceili(seconds / PLAYER_SCRIPT.BOMB_RATE)
	assert_int(host_bombs).is_less_equal(
		runners.size() * max_bombs_per_player,
	)


func test_stunned_ai_cannot_place_bombs() -> void:
	var runners := await add_players_and_start(2)

	# Stun jose from the host (server authority).
	var jose_on_host := await game.host.await_player(&"jose", 2.0) as Node2D
	jose_on_host.exploded.rpc(game.host.peer_id)
	await game.sync_ticks(2)
	assert_bool(is_stunned(runners[1], &"jose")).is_true()

	var jose_ai := BomberAI.create(runners[1], &"jose")
	jose_ai.goal = BomberAI.Goal.score()
	jose_ai.flee_enabled = false

	var bombs_before := count_bombs(game.host)
	await run_until([jose_ai], 10)
	var bombs_after := count_bombs(game.host)

	# The stunned player spawned no new bombs.
	assert_int(bombs_after).is_less_equal(bombs_before)
