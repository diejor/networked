extends NetwTestSuite

const MAIN := preload("res://examples/whiteboard/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_two_drawers_draw_at_once() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	var bo := await game.add_client("bo", false)
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	await settle(peers)

	ada.simulate_action_press("draw")
	bo.simulate_action_press("draw")
	await game.sync_ticks(1)
	assert_object(drawn_by(ada, ada)).override_failure_message(
		"the stroke appears on the press, before any reply",
	).is_not_null()
	await stroke([ada, bo], 6)
	ada.simulate_action_release("draw")
	bo.simulate_action_release("draw")
	await game.sync_ticks(30)

	for peer in peers:
		assert_int(lines(peer).size()).override_failure_message(
			"%s holds %d strokes" % [peer.username, lines(peer).size()],
		).is_equal(2)
		for drawer in [ada, bo]:
			assert_array(Array(drawn_by(peer, drawer).points)).is_equal(
				Array(drawn_by(drawer, drawer).points),
			)
	assert_int(drawn_by(ada, ada).points.size()).is_equal(6)
	assert_int(drawn_by(bo, bo).points.size()).is_equal(6)


func test_a_drawer_erases_only_their_own_strokes() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	var bo := await game.add_client("bo", false)
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	await settle(peers)

	ada.simulate_action_press("draw")
	bo.simulate_action_press("draw")
	await game.sync_ticks(1)
	await stroke([ada, bo], 4)
	ada.simulate_action_release("draw")
	bo.simulate_action_release("draw")
	await game.sync_ticks(30)

	board(ada).erase_mine()
	await game.sync_ticks(30)
	for peer in peers:
		assert_int(lines(peer).size()).override_failure_message(
			"%s holds %d strokes" % [peer.username, lines(peer).size()],
		).is_equal(1)
		assert_object(drawn_by(peer, bo)).is_not_null()

	board(host).clear()
	await game.sync_ticks(30)
	for peer in peers:
		assert_int(lines(peer).size()).is_equal(0)


func test_a_late_joiner_sees_the_strokes() -> void:
	var host := await game.add_host("hana", false)
	var ada := await game.add_client("ada", false)
	await settle([host, ada])

	ada.simulate_action_press("draw")
	await game.sync_ticks(1)
	await stroke([ada], 5)
	ada.simulate_action_release("draw")
	await game.sync_ticks(30)
	assert_int(drawn_by(host, ada).points.size()).is_equal(5)

	var bo := await game.add_client("bo", false)
	await settle([bo])
	await game.sync_ticks(40)
	assert_int(lines(bo).size()).is_equal(1)
	assert_array(Array(drawn_by(bo, ada).points)).is_equal(
		Array(drawn_by(host, ada).points),
	)


func settle(peers: Array) -> void:
	for peer: NetwSceneRunner in peers:
		await peer.await_scene(&"Room", 2.0)
	await game.sync_ticks(10)


func stroke(drawers: Array[NetwSceneRunner], points: int) -> void:
	for at in points:
		for index in drawers.size():
			var from := Vector2(100 + 500 * index, 100)
			drawers[index].simulate_mouse_move(from + Vector2(at * 8, at * 3))
		await game.sync_ticks(1)


func board(peer: NetwSceneRunner) -> Whiteboard:
	return Netw.scene(peer.tree, &"Room").root.get_node(^"Whiteboard")


func lines(peer: NetwSceneRunner) -> Array[Node]:
	return board(peer).strokes.get_children()


func drawn_by(peer: NetwSceneRunner, drawer: NetwSceneRunner) -> Stroke:
	var id := board(drawer).multiplayer.get_unique_id()
	for line: Stroke in lines(peer):
		if line.entity.controller == id:
			return line
	return null
