class_name TestBomberGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


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

	var frozen_x := player.position.x
	player.exploded(valeria.peer_id)
	await game.sync_ticks(2)
	assert_that(player.get("stunned")).is_true()

	valeria.simulate_action_press("move_right")
	await game.sync_ticks(8)
	valeria.simulate_action_release("move_right")
	assert_float(player.position.x).is_less_equal(frozen_x + 1.0)


func test_client_input_drives_only_its_player_and_spawns_rate_limited_bomb() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	var valeria_world: NetwSceneHandle = Netw.scene(valeria.tree, &"World")
	await jose.await_scene(&"World")
	var jose_world: NetwSceneHandle = Netw.scene(jose.tree, &"World")
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

	assert_that(jose_player.position.x).is_greater(jose_start)
	var host_entity := NetwEntity.of(jose_on_host)
	var host_api := NetwMultiplayer.core_of(jose_on_host)
	var pump_mode: int = host_api.display_get_track_stat(
		host_entity.rid,
		&"",
		&"pump_mode",
	)
	assert_int(pump_mode).override_failure_message(
		"the host must sample the client player it simulates authoritatively",
	).is_equal(NetwMultiplayer.DISPLAY_PUMP_BRACKETED)
	assert_float(valeria_player.position.x).is_equal_approx(valeria_held, 1.0)

	var host_bombs := _count_bombs(valeria, valeria_world)
	var client_bombs := _count_bombs(jose, jose_world)

	assert_int(host_bombs).is_greater(0)
	assert_int(host_bombs).is_less_equal(2)
	assert_int(client_bombs).is_equal(host_bombs)


func test_rough_link_keeps_bombs_reliable_and_positions_converging() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	await jose.await_scene(&"World")
	var jose_world: NetwSceneHandle = Netw.scene(jose.tree, &"World")
	var valeria_player := await valeria.await_player(&"valeria") as Node2D
	var valeria_on_jose := await jose.await_player(&"valeria") as Node2D

	game.path(valeria, jose) \
			.loss(0.5) \
			.latency_ms(66.0) \
			.seed(1)

	await game.sync_ticks(8)
	valeria.simulate_action_press("move_right")
	valeria.simulate_action_press("set_bomb")
	var bomb_seen := await _wait_for_bomb(jose, jose_world, 32)
	valeria.simulate_action_release("move_right")
	valeria.simulate_action_release("set_bomb")

	assert_bool(bomb_seen).is_true()
	await game.sync_ticks(40)
	assert_float(valeria_on_jose.position.x).is_equal_approx(
		valeria_player.position.x,
		8.0,
	)


func test_server_explosion_scores_rocks_and_stuns_players_across_peers() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	var world: NetwSceneHandle = Netw.scene(valeria.tree, &"World")
	var jose_view := await jose.await_player(&"jose") as Node2D
	var jose_on_host := valeria.find_player(&"jose") as Node2D

	await game.sync_ticks(8)
	valeria.simulate_action_press("set_bomb")
	await game.sync_ticks(6)
	valeria.simulate_action_release("set_bomb")
	await game.sync_ticks(2)

	var bomb := _first_bomb(valeria, world)
	assert_that(bomb).is_not_null()

	var score := world.root.get_node("Score")
	var rock := world.root.get_node("Rocks").get_child(0)
	var score_before: int = score.get_score(valeria.peer_id)

	bomb.from_player = valeria.peer_id
	bomb.in_area = [rock, jose_on_host]
	bomb.explode()
	await game.sync_ticks(6)

	assert_int(score.get_score(valeria.peer_id)).is_greater(score_before)
	assert_that(jose_on_host.get("stunned")).is_true()
	assert_that(jose_view.get("stunned")).is_true()


func test_client_disconnect_keeps_match_running() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World")
	await valeria.await_player(&"jose")

	var gamestate := (
			Netw.service(valeria.tree, BomberGamestate) as BomberGamestate
	)
	var errored: Array[bool] = [false]
	gamestate.game_error.connect(func(_what: String) -> void: errored[0] = true)

	await game.disconnect_runner(jose)
	var dropped := false
	for i in 60:
		await game.sync_ticks(1)
		if valeria.find_player(&"jose") == null:
			dropped = true
			break

	assert_bool(dropped).is_true()
	assert_bool(errored[0]).is_false()
	assert_bool(gamestate.world.is_declared).is_true()
	assert_that(valeria.find_player(&"valeria")).is_not_null()
	await drain_frames(get_tree(), 10)


func test_host_lobby_ui_spawns_inside_scene_with_roster() -> void:
	var valeria := await game.add_host("valeria", false)
	await drain_frames(get_tree(), 5)

	var in_lobby := valeria.scene().find_child("InLobby", true, false)
	assert_that(in_lobby).is_not_null()
	var member_list := in_lobby.find_child("MemberList", true, false) as ItemList
	assert_int(member_list.item_count).is_equal(1)

	var browser := valeria.scene().find_child("ConnectBrowser", true, false) as Control
	assert_bool(browser.visible).is_false()
	assert_that(
		Netw.session(valeria.tree).presented_scene.label,
	).is_equal(&"Lobby")


func test_exit_button_returns_every_peer_to_the_lobby() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	await _begin_game(valeria)

	await valeria.await_scene(&"World", 2.0)
	await jose.await_scene(&"World", 2.0)

	press_exit(valeria)
	assert_bool(await await_match_over(valeria)).is_true()
	assert_bool(await await_match_over(jose)).is_true()

	assert_that(
		Netw.session(valeria.tree).presented_scene.label,
	).is_equal(&"Lobby")
	assert_that(
		Netw.session(jose.tree).presented_scene.label,
	).is_equal(&"Lobby")
	assert_that(valeria.find_player(&"valeria")).is_null()
	assert_that(jose.find_player(&"jose")).is_null()


func test_a_mid_match_joiner_waits_in_the_lobby_and_plays_the_next_round() -> void:
	var valeria := await game.add_host("valeria", false)
	await _begin_game(valeria)
	await valeria.await_scene(&"World", 2.0)

	var jose := await game.add_client("jose", false)
	await jose.await_scene(&"Lobby", 2.0)
	assert_that(
		Netw.session(jose.tree).presented_scene.label,
	).is_equal(&"Lobby")
	assert_that(valeria.find_player(&"jose")).is_null()

	press_exit(valeria)
	assert_bool(await await_match_over(valeria)).is_true()

	var gamestate := (
			Netw.service(valeria.tree, BomberGamestate) as BomberGamestate
	)
	gamestate.begin_game()

	await valeria.await_scene(&"World", 2.0)
	await jose.await_scene(&"World", 2.0)
	assert_that(await jose.await_player(&"jose", 2.0)).is_not_null()
	assert_that(await valeria.await_player(&"valeria", 2.0)).is_not_null()


func test_every_peer_runs_each_player_in_its_own_cell() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	var ana := await game.add_client("ana", false)
	await _begin_game(valeria)

	for runner: NetwSceneRunner in [valeria, jose, ana]:
		await runner.await_scene(&"World", 2.0)
		for name: StringName in [&"valeria", &"jose", &"ana"]:
			await runner.await_player(name, 2.0)
	await game.sync_ticks(8)

	var jose_own := jose.find_player(&"jose") as Node2D
	var jose_on_host := valeria.find_player(&"jose") as Node2D
	var jose_on_ana := ana.find_player(&"jose") as Node2D
	var start := jose_own.position.x
	jose.simulate_action_press("move_right")
	await game.sync_ticks(8)

	var own := NetwSimulationHandle.MODE_PREDICT
	var host := NetwSimulationHandle.MODE_AUTHORITY
	var other := NetwSimulationHandle.MODE_PROXY
	var bracketed := NetwMultiplayer.DISPLAY_PUMP_BRACKETED
	var remote := NetwMultiplayer.DISPLAY_PUMP_REMOTE
	var peers := { &"valeria": valeria, &"jose": jose, &"ana": ana }
	var cells := [
		[&"valeria", &"valeria", host, bracketed],
		[&"valeria", &"jose", host, bracketed],
		[&"valeria", &"ana", host, bracketed],
		[&"jose", &"jose", own, bracketed],
		[&"jose", &"valeria", other, remote],
		[&"jose", &"ana", other, remote],
		[&"ana", &"ana", own, bracketed],
		[&"ana", &"valeria", other, remote],
		[&"ana", &"jose", other, remote],
	]
	for cell: Array in cells:
		var runner: NetwSceneRunner = peers[cell[0]]
		var player := runner.find_player(cell[1]) as Node2D
		var entity := NetwEntity.of(player)
		var pump: int = NetwMultiplayer.core_of(player).display_get_track_stat(
			entity.rid,
			&"",
			&"pump_mode",
		)
		var mode: int = entity.simulation.mode
		assert_int(mode).override_failure_message(
			"%s runs %s in mode %d" % [cell[0], cell[1], mode],
		).is_equal(cell[2])
		assert_int(pump).override_failure_message(
			"%s draws %s through pump %d" % [cell[0], cell[1], pump],
		).is_equal(cell[3])

	jose.simulate_action_release("move_right")
	await game.sync_ticks(30)
	assert_float(jose_own.position.x).is_greater(start)
	assert_float(jose_on_host.position.x).is_equal_approx(
		jose_own.position.x,
		8.0,
	)
	assert_float(jose_on_ana.position.x).is_equal_approx(
		jose_own.position.x,
		8.0,
	)


func press_exit(host: NetwSceneRunner) -> void:
	var world: NetwSceneHandle = Netw.scene(host.tree, &"World")
	var exit := world.root.get_node(^"Winner/ExitGame") as Button
	exit.pressed.emit()


func await_match_over(runner: NetwSceneRunner) -> bool:
	for i in 120:
		await game.sync_ticks(1)
		if Netw.scene(runner.tree, &"World") == null:
			return true
	return false


func test_the_reachability_report_answers_for_the_tick_path() -> void:
	var valeria := await game.add_host("valeria", false)
	await _begin_game(valeria)
	await valeria.await_scene(&"World", 2.0)
	var player := await valeria.await_player(&"valeria", 2.0) as Node2D

	var report: Dictionary = NetwEntity.of(player).prediction.reachability()
	assert_dict(report).override_failure_message(
		"a predicted entity must be able to answer for its own declarations",
	).is_not_empty()
	var fields: Dictionary = report[&"fields"]

	assert_float(float(fields[&"position"][&"tolerance"])).is_equal(4.0)
	assert_bool(bool(fields[&"position"][&"tolerance_declared"])).is_true()
	assert_float(float(fields[&"position"][&"teleport_at"])) \
			.override_failure_message(
				"position declares its tier distance in its own units, so the "
				+ "report must not answer with the entity default",
			).is_equal(48.0)
	assert_bool(bool(fields[&"position"][&"triggers"])).is_true()

	assert_str(String(fields[&"velocity"][&"class"])).is_equal("DERIVED")
	assert_bool(bool(fields[&"velocity"][&"triggers"])).override_failure_message(
		"velocity is recomputed from the input every tick, so it may be "
		+ "replicated for observers but must not decide that a correction is "
		+ "needed",
	).is_false()

	var model: Dictionary = fields[&"position"][&"forward_model"]
	assert_str(String(model[&"kind"])).override_failure_message(
		"the game declares no step, so the report must not invent one",
	).is_equal("none")


func _begin_game(host: NetwSceneRunner) -> void:
	await host.await_scene(&"Lobby", 2.0)
	var gamestate := (
			Netw.service(host.tree, BomberGamestate) as BomberGamestate
	)
	assert_that(gamestate).is_not_null()
	gamestate.begin_game()


func _count_bombs(runner, world: NetwSceneHandle) -> int:
	var bombs := world.root.get_node_or_null("Bombs")
	return bombs.get_child_count() if bombs else 0


func _wait_for_bomb(runner, world: NetwSceneHandle, ticks: int) -> bool:
	for i in ticks:
		await game.sync_ticks(1)
		if _count_bombs(runner, world) > 0:
			return true
	return false


func _first_bomb(runner, world: NetwSceneHandle) -> Area2D:
	var bombs := world.root.get_node_or_null("Bombs")
	if not bombs:
		return null
	for child in bombs.get_children():
		if child is Area2D:
			return child
	return null
