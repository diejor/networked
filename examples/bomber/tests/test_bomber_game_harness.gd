class_name TestBomberGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


# A player spawned by begin_game answers to its owner's input, and a bomb
# explosion freezes that player so movement input no longer moves it. This is
# the whole bomber loop for one peer: spawn, drive, get bombed, lose control.
func test_match_player_moves_until_a_bomb_freezes_it() -> void:
	var valeria := await game.add_host("valeria", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World", 2.0)
	var player := await valeria.await_player(&"valeria", 2.0) as Node2D
	assert_that(valeria.local_player).is_equal(player)

	var start := player.position.x
	await game.sync_ticks(8)
	valeria.simulate_action_press("move_right")
	await game.sync_ticks(8)
	valeria.simulate_action_release("move_right")

	assert_that(player.position.x).is_greater(start)
	assert_that(player.get("stunned")).is_false()

	# A bomb in range reaches the player through exploded(). Drive it directly
	# so the assertion pins the consequence, not the bomb's animation timing.
	var frozen_x := player.position.x
	player.exploded(valeria.peer_id)
	await game.sync_ticks(2)
	assert_that(player.get("stunned")).is_true()

	valeria.simulate_action_press("move_right")
	await game.sync_ticks(8)
	valeria.simulate_action_release("move_right")
	assert_float(player.position.x).is_less_equal(frozen_x + 1.0)


# A client's input is scoped to its own player: it moves that player and spawns
# its rate-limited, server-authoritative bomb, while the host's player never
# reacts. The BOMB_RATE gate holds a held button to a couple of bombs, not one
# per tick, and the spawn replicates back to the host.
func test_client_input_drives_only_its_player_and_spawns_rate_limited_bomb() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	var valeria_world := await valeria.await_scene(&"World")
	var jose_world := await jose.await_scene(&"World")
	var jose_player := await jose.await_player(&"jose") as Node2D
	var valeria_player := valeria.find_player(&"valeria") as Node2D
	var jose_on_host := valeria.find_player(&"jose") as Node2D

	await game.sync_ticks(8)
	var jose_start := jose_player.position.x
	var valeria_held := valeria_player.position.x

	jose.simulate_action_press("move_right")
	jose.simulate_action_press("set_bomb")
	await game.sync_ticks(12)
	jose.simulate_action_release("move_right")
	jose.simulate_action_release("set_bomb")
	await game.sync_ticks(4)

	# The client drives its own player.
	assert_that(jose_player.position.x).is_greater(jose_start)
	var host_runtime = NetwEntity.of(jose_on_host).interpolation._runtime()
	assert_int(host_runtime.pump_mode).override_failure_message(
		"the host must sample the client player it simulates authoritatively",
	).is_equal(NetwInterpolationInterface._PUMP_BRACKETED)
	# And only its own: the host's player never saw the input.
	assert_float(valeria_player.position.x).is_equal_approx(valeria_held, 1.0)

	var host_bombs := _count_bombs(valeria_world)
	var client_bombs := _count_bombs(jose_world)

	# Reaches the host at all, so the spawn is server driven and replicated.
	assert_int(host_bombs).is_greater(0)
	# The cooldown holds: a held button does not spawn a bomb every tick.
	assert_int(host_bombs).is_less_equal(2)
	# Both peers see the same bombs, so neither side spawned locally.
	assert_int(client_bombs).is_equal(host_bombs)


# Under a rough link, the two transfer modes degrade differently and the test
# pins both: the reliable bomb spawn still arrives, while the unreliable
# position stream self-heals so the client's view converges once motion stops.
func test_rough_link_keeps_bombs_reliable_and_positions_converging() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	var jose_world := await jose.await_scene(&"World")
	var valeria_player := await valeria.await_player(&"valeria") as Node2D
	var valeria_on_jose := await jose.await_player(&"valeria") as Node2D

	game.path(valeria, jose) \
			.loss(0.5) \
			.latency_ms(66.0) \
			.seed(1)

	await game.sync_ticks(8)
	valeria.simulate_action_press("move_right")
	valeria.simulate_action_press("set_bomb")
	var bomb_seen := await _wait_for_bomb(jose_world, 32)
	valeria.simulate_action_release("move_right")
	valeria.simulate_action_release("set_bomb")

	# Reliable spawn punches through the lossy link.
	assert_bool(bomb_seen).is_true()
	await game.sync_ticks(40)
	# Unreliable position stream recovers to the authoritative value.
	assert_float(valeria_on_jose.position.x).is_equal_approx(
		valeria_player.position.x,
		8.0,
	)


# An explosion is the server's to resolve, and it lands on every kind of target
# at once: a rock in the blast scores for the bomber, a player in the blast is
# stunned, and that stun replicates to the player's own client. The line of
# sight check does not spare a target behind a wall here, which documents that
# Layer0 is a TileMapLayer and bomb.gd only blocks on a TileMap.
func test_server_explosion_scores_rocks_and_stuns_players_across_peers() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	var world := await valeria.await_scene(&"World")
	var jose_view := await jose.await_player(&"jose") as Node2D
	var jose_on_host := valeria.find_player(&"jose") as Node2D

	await game.sync_ticks(8)
	valeria.simulate_action_press("set_bomb")
	await game.sync_ticks(6)
	valeria.simulate_action_release("set_bomb")
	await game.sync_ticks(2)

	var bomb := _first_bomb(world)
	assert_that(bomb).is_not_null()

	var score := world.level.get_node("Score")
	var rock := world.level.get_node("Rocks").get_child(0)
	var score_before: int = score.get_score(valeria.peer_id)

	# Resolve the blast on the server against a rock and the remote player.
	bomb.from_player = valeria.peer_id
	bomb.in_area = [rock, jose_on_host]
	bomb.explode()
	await game.sync_ticks(6)

	# The rock scored for the bomber, and the player was stunned on the server.
	assert_int(score.get_score(valeria.peer_id)).is_greater(score_before)
	assert_that(jose_on_host.get("stunned")).is_true()
	# The stun reached the player's own client.
	assert_that(jose_view.get("stunned")).is_true()


# Losing a player mid-match is fatal to the round: the server reports the error
# and tears the world down rather than playing on a peer short.
func test_client_disconnect_keeps_match_running() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	await valeria.await_player(&"jose")

	var gamestate := valeria.tree.get_service(BomberGamestate) as BomberGamestate
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(func(_what: String) -> void: errored[0] = true)

	# A client leaving drops only that player; the match continues for the host.
	await game.disconnect_runner(jose)
	var dropped := false
	for i in 60:
		await game.sync_ticks(1)
		if valeria.find_player(&"jose") == null:
			dropped = true
			break

	assert_bool(dropped).is_true()
	assert_bool(errored[0]).is_false()
	assert_that(gamestate.world).is_not_null()
	assert_that(valeria.find_player(&"valeria")).is_not_null()
	await drain_frames(get_tree(), 10)


# The lobby roster now ships inside the networked Lobby scene, so admitting the
# host must (1) spawn that scene's UI and (2) populate its roster, while the
# pre-session browser steps aside. On a listen server admission runs
# synchronously inside participant_joined, so local_scene_changed has to be
# relayed by then. Regression for binding local_participant after the emit, which
# dropped the host's first scene change and left it stuck on the browser.
func test_host_lobby_ui_spawns_inside_scene_with_roster() -> void:
	var valeria := await game.add_host("valeria", false)
	await drain_frames(get_tree(), 5)

	var in_lobby := valeria.scene().find_child("InLobby", true, false)
	assert_that(in_lobby).is_not_null()
	var member_list := in_lobby.find_child("MemberList", true, false) as ItemList
	assert_int(member_list.item_count).is_equal(1)

	var browser := valeria.scene().find_child("ConnectBrowser", true, false) as Control
	assert_bool(browser.visible).is_false()
	assert_that(valeria.tree.local_participant.current_scene.scene_name) \
			.is_equal(&"Lobby")


func _begin_game(host: NetwSceneRunner) -> void:
	# The Start button lives inside the lobby scene, so a real match start
	# always begins from a spawned lobby. Awaiting it keeps a programmatic
	# start from racing the startup scene spawn.
	await host.await_scene(&"Lobby", 2.0)
	var gamestate := host.tree.get_service(BomberGamestate) as BomberGamestate
	assert_that(gamestate).is_not_null()
	gamestate.begin_game()


func _count_bombs(world: MultiplayerScene) -> int:
	var bombs := world.level.get_node_or_null("Bombs")
	return bombs.get_child_count() if bombs else 0


func _wait_for_bomb(world: MultiplayerScene, ticks: int) -> bool:
	for i in ticks:
		await game.sync_ticks(1)
		if _count_bombs(world) > 0:
			return true
	return false


func _first_bomb(world: MultiplayerScene) -> Area2D:
	var bombs := world.level.get_node_or_null("Bombs")
	if not bombs:
		return null
	for child in bombs.get_children():
		if child is Area2D:
			return child
	return null
