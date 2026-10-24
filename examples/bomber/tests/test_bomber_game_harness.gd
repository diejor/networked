class_name TestBomberGameHarness
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_an_accepted_invite_joins_the_host() -> void:
	var host := await game.add_host("host", false)
	var guest := MultiplayerViewport.new()
	guest.add_child(MAIN.instantiate())
	add_child(guest)
	var session: NetwSession = Netw.session(guest)
	await get_tree().process_frame

	session.connection.invite(&"LocalMultiplayerPeer", "local", "host")
	for i in 120:
		if session.local_player != null:
			break
		await game.sync_ticks(1)

	assert_bool(session.is_online).is_true()
	assert_object(session.local_player).is_not_null()
	assert_int(host.session.players.size()).is_equal(2)

	await session.leave().wait()
	guest.queue_free()
	await get_tree().process_frame


func test_client_moves_and_drops_a_bomb_every_peer_sees() -> void:
	var host := await game.add_host("host", false)
	var client := await game.add_client("client", false)
	await start_match(host)
	var host_world := await host.await_scene(&"World")
	var client_world := await client.await_scene(&"World")
	var player := await client.await_player(&"client") as Node2D
	var host_player := host.find_player(&"host") as Node2D
	await game.sync_ticks(8)
	var start := player.position.x
	var host_start := host_player.position.x

	client.simulate_action_press("move_right")
	client.simulate_action_press("set_bomb")
	await game.sync_ticks(12)
	client.simulate_action_release("move_right")
	client.simulate_action_release("set_bomb")
	await game.sync_ticks(4)

	assert_float(player.position.x).is_greater(start)
	assert_float(host_player.position.x).is_equal(host_start)
	assert_int(bombs(host_world)).is_greater(0)
	assert_int(bombs(client_world)).is_equal(bombs(host_world))


func test_explosion_scores_rocks_and_stuns_players() -> void:
	var host := await game.add_host("host", false)
	var client := await game.add_client("client", false)
	await start_match(host)
	var world := await host.await_scene(&"World")
	var client_view := await client.await_player(&"client") as Node2D
	var client_on_host := host.find_player(&"client") as Node2D
	await game.sync_ticks(8)

	host.simulate_action_press("set_bomb")
	await game.sync_ticks(6)
	host.simulate_action_release("set_bomb")
	await game.sync_ticks(2)
	var bomb = world.root.get_node(^"Bombs").get_child(0)
	var score = world.root.get_node(^"Score")
	var before: int = score.get_score(host.session.local_player)

	bomb.from_player = host.session.local_player.peer_id
	bomb.in_area = [world.root.get_node(^"Rocks").get_child(0), client_on_host]
	bomb.explode()
	await game.sync_ticks(6)

	assert_int(score.get_score(host.session.local_player)).is_greater(before)
	assert_bool(client_on_host.get(&"stunned")).is_true()
	assert_bool(client_view.get(&"stunned")).is_true()


func test_exit_returns_every_peer_to_the_lobby() -> void:
	var host := await game.add_host("host", false)
	var client := await game.add_client("client", false)
	await start_match(host)
	await client.await_scene(&"World", 2.0)

	var world: NetwScene = host.session.find_scene(&"World")
	(world.root.get_node(^"Winner/ExitGame") as Button).pressed.emit()
	for i in 120:
		if lobby(host).visible and lobby(client).visible:
			break
		await game.sync_ticks(1)

	assert_bool(lobby(host).visible).is_true()
	assert_bool(lobby(client).visible).is_true()
	assert_that(client.find_player(&"client")).is_null()


func test_a_host_leaving_mid_match_hides_the_client_lobby() -> void:
	var host := await game.add_host("host", false)
	var client := await game.add_client("client", false)
	await start_match(host)
	await client.await_scene(&"World", 2.0)

	host.session.leave()
	for i in 120:
		if not client.session.is_online:
			break
		await game.sync_ticks(1)
	await game.sync_ticks(4)

	assert_bool(client.session.is_online).is_false()
	assert_bool(lobby(client).visible).is_false()


func start_match(host: NetwSceneRunner) -> void:
	for i in 120:
		if lobby(host).visible:
			break
		await game.sync_ticks(1)
	(host.session.service(BomberGamestate) as BomberGamestate).begin_game()


func lobby(runner: NetwSceneRunner) -> InLobby:
	return runner.scene().find_child("InLobby", true, false) as InLobby


func bombs(world: NetwScene) -> int:
	return world.root.get_node(^"Bombs").get_child_count()
