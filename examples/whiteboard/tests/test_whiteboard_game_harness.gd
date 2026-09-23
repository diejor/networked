extends NetwTestSuite

const MAIN := preload("res://examples/whiteboard/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_a_stroke_reaches_every_peer_after_release() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	var bo := await game.add_client("bo", false)
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	await settle(peers)

	ada.simulate_action_press("draw")
	await game.sync_ticks(1)
	assert_bool(board(ada).drawing).override_failure_message(
		"drawing starts on the press, before any reply",
	).is_true()
	await stroke(ada, Vector2(100, 100), 6)
	ada.simulate_action_release("draw")
	await game.sync_ticks(30)

	var drawn := board(ada).ink
	assert_int(drawn.size()).is_equal(6)
	for peer in peers:
		assert_int(board(peer).entity.controller).is_equal(0)
		assert_array(Array(board(peer).ink)).override_failure_message(
			"%s holds %d points" % [peer.username, board(peer).ink.size()],
		).is_equal(Array(drawn))
		assert_array(Array(board(peer).breaks)).is_equal([0])


func test_a_refused_stroke_is_erased_on_the_refused_peer() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	var bo := await game.add_client("bo", false)
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	await settle(peers)
	game.degrade(ada).latency_ms(200.0)
	await game.sync_ticks(20)

	bo.simulate_action_press("draw")
	ada.simulate_action_press("draw")
	await game.sync_ticks(1)
	assert_bool(board(ada).drawing).is_true()
	await stroke(ada, Vector2(500, 300), 4)
	assert_int(board(ada).ink.size()).override_failure_message(
		"ada draws ahead of the decision",
	).is_equal(4)
	await stroke(bo, Vector2(10, 10), 4)

	await game.sync_ticks(40)
	assert_bool(board(ada).drawing).override_failure_message(
		"the refusal stops ada drawing",
	).is_false()
	for point in board(ada).ink:
		assert_float(point.x).override_failure_message(
			"ada's refused stroke is still on ada's board",
		).is_less(500.0)

	bo.simulate_action_release("draw")
	await game.sync_ticks(40)
	var drawn := board(bo).ink
	assert_int(drawn.size()).is_equal(4)
	for peer in peers:
		assert_int(board(peer).entity.controller).is_equal(0)
		assert_array(Array(board(peer).ink)).override_failure_message(
			"%s holds %d points" % [peer.username, board(peer).ink.size()],
		).is_equal(Array(drawn))


func test_a_late_joiner_sees_the_board() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	await settle([host, ada])

	ada.simulate_action_press("draw")
	await game.sync_ticks(1)
	await stroke(ada, Vector2(200, 200), 5)
	ada.simulate_action_release("draw")
	await game.sync_ticks(30)
	assert_int(board(host).ink.size()).is_equal(5)

	var bo := await game.add_client("bo", false)
	await settle([bo])
	await game.sync_ticks(40)
	assert_array(Array(board(bo).ink)).override_failure_message(
		"a late joiner holds %d points" % board(bo).ink.size(),
	).is_equal(Array(board(host).ink))
	assert_array(Array(board(bo).breaks)).is_equal([0])


func settle(peers: Array) -> void:
	for peer: NetwSceneRunner in peers:
		await peer.await_scene(&"Room", 2.0)
	await game.sync_ticks(10)


func stroke(peer: NetwSceneRunner, from: Vector2, points: int) -> void:
	for at in points:
		peer.simulate_mouse_move(from + Vector2(at * 8, at * 3))
		await game.sync_ticks(1)


func board(peer: NetwSceneRunner) -> Whiteboard:
	return Netw.scene(peer.tree, &"Room").root.get_node(^"Whiteboard")
