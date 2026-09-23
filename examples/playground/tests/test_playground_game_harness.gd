extends NetwTestSuite

const MAIN := preload("res://examples/playground/main.tscn")
const STACK: Array[int] = [5, 9, 13]
const STACK_BASE := Vector3(-2.25, 0.25, 0.75)
const THROWER_AT := Vector3(-2.25, 1.3, 3.65)

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_a_grab_by_one_client_is_seen_on_every_peer() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, ada, bo]

	await grab(ada, 3, Vector3.BACK)
	assert_int(cube(ada, 3).held_by).override_failure_message(
		"a grab pins the cube in the hand before any reply",
	).is_equal(ada.peer_id)
	await game.sync_ticks(20)

	for peer in peers:
		var seen := cube(peer, 3)
		assert_int(seen.entity.controller).is_equal(ada.peer_id)
		assert_int(seen.held_by).override_failure_message(
			"%s reads held_by %d" % [peer.username, seen.held_by],
		).is_equal(ada.peer_id)
		assert_bool(seen.freeze).is_true()

	var start := cube(bo, 3).global_position
	ada.simulate_action_press("left")
	await game.sync_ticks(30)
	ada.simulate_action_release("left")
	await game.sync_ticks(20)
	var carried := cube(ada, 3).global_position
	assert_float(start.x - carried.x).override_failure_message(
		"the hand carries the cube",
	).is_greater(1.0)
	for peer in peers:
		assert_float(cube(peer, 3).global_position.distance_to(carried)) \
				.override_failure_message(
					"%s draws the held cube %.2f m from the hand"
					% [
						peer.username,
						cube(peer, 3).global_position.distance_to(carried),
					],
				).is_less(0.1)


func test_a_thrown_cube_claims_the_cube_it_knocks() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	var target_start := cube(host, 7).global_position

	await grab(ada, 3, Vector3.BACK)
	await game.sync_ticks(20)
	ada.simulate_action_release("grab")
	for _tick in 90:
		await game.sync_ticks(1)
		if cube(host, 7).entity.controller == ada.peer_id:
			break
	await game.sync_ticks(10)

	for peer in peers:
		assert_int(cube(peer, 7).entity.controller).override_failure_message(
			"%s reads cube 7 controlled by %d"
			% [peer.username, cube(peer, 7).entity.controller],
		).is_equal(ada.peer_id)
		assert_int(cube(peer, 7).entity.hold).is_equal(
			NetwEntity.HOLD_YIELDABLE,
		)
	assert_float(target_start.z - cube(host, 7).global_position.z) \
			.override_failure_message(
				"the knocked cube moves on the host",
			).is_greater(0.2)


func test_two_grabs_in_one_tick_leave_exactly_one_holder() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	game.degrade(ada).latency_ms(100.0)
	game.degrade(bo).latency_ms(100.0)
	await game.sync_ticks(30)

	face(ada, 1, Vector3.BACK)
	face(bo, 1, Vector3.LEFT)
	await game.sync_ticks(4)
	ada.simulate_action_press("grab")
	bo.simulate_action_press("grab")
	await game.sync_ticks(1)
	assert_int(cube(host, 1).entity.controller).is_equal(0)
	assert_int(cube(ada, 1).held_by).is_equal(ada.peer_id)
	assert_int(cube(bo, 1).held_by).is_equal(bo.peer_id)

	var seen := {ada: [], bo: []}
	for _tick in 60:
		await game.sync_ticks(1)
		for peer: NetwSceneRunner in seen:
			seen[peer].append(cube(peer, 1).held_by)

	var winner_id := cube(host, 1).entity.controller
	assert_array([ada.peer_id, bo.peer_id]).contains([winner_id])
	var winner := ada if winner_id == ada.peer_id else bo
	var loser := bo if winner == ada else ada
	for peer in peers:
		assert_int(cube(peer, 1).entity.controller).is_equal(winner_id)
		assert_int(cube(peer, 1).held_by).override_failure_message(
			"%s reads held_by %d" % [peer.username, cube(peer, 1).held_by],
		).is_equal(winner_id)
	assert_object(cube(loser, 1).hand).is_null()
	assert_float(
		cube(loser, 1).global_position.distance_to(
			cube(winner, 1).global_position,
		),
	).is_less(0.1)

	var trail: Array = seen[loser]
	var released := trail.find(0)
	assert_int(released).override_failure_message(
		"the loser's cube returns to the accepted held_by 0 before the "
		+ "winner's stream takes it, and read %s" % [trail],
	).is_greater(0)
	for held_by: int in trail.slice(0, released):
		assert_int(held_by).is_equal(loser.peer_id)
	assert_bool(trail.slice(released).has(loser.peer_id)).is_false()
	assert_int(trail.back()).is_equal(winner_id)


func test_cubes_at_rest_return_to_the_session() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, ada, bo]

	await grab(ada, 4, Vector3.BACK)
	await game.sync_ticks(20)
	ada.simulate_action_release("grab")
	await game.sync_ticks(30)
	assert_int(cube(host, 4).entity.controller).is_equal(ada.peer_id)
	assert_int(cube(host, 8).entity.controller).is_equal(ada.peer_id)

	for _tick in 900:
		await game.sync_ticks(1)
		if claimed(peers).is_empty():
			break
	await game.sync_ticks(10)

	assert_array(claimed(peers)).override_failure_message(
		"cubes still claimed after rest %s" % [claimed(peers)],
	).is_empty()
	for n in [4, 8]:
		for peer in peers:
			assert_float(
				cube(peer, n).global_position.distance_to(
					cube(host, n).global_position,
				),
			).override_failure_message(
				"%s rests cube %d away from the host" % [peer.username, n],
			).is_less(0.05)


func test_a_late_joiner_sees_who_holds_a_cube() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	await grab(ada, 2, Vector3.BACK)
	await game.sync_ticks(20)
	assert_int(cube(host, 2).held_by).is_equal(ada.peer_id)

	var bo := await game.add_client("bo")
	await bo.await_scene(&"Playground", 2.0)
	await game.sync_ticks(40)

	assert_int(cube(bo, 2).entity.controller).is_equal(ada.peer_id)
	assert_int(cube(bo, 2).held_by).override_failure_message(
		"a late joiner reads held_by %d" % cube(bo, 2).held_by,
	).is_equal(ada.peer_id)
	assert_bool(cube(bo, 2).freeze).is_true()
	assert_float(
		cube(bo, 2).global_position.distance_to(cube(ada, 2).global_position),
	).is_less(0.1)


func test_a_contested_cube_thrown_into_a_stack_knocks_it_at_impact() -> void:
	var host := await game.add_host("hana")
	for n in STACK.size():
		cube(host, STACK[n]).global_position = STACK_BASE + Vector3.UP * 0.5 * n
	await game.sync_ticks(60)
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, ada, bo]
	game.degrade(ada).latency_ms(100.0)
	game.degrade(bo).latency_ms(100.0)
	await game.sync_ticks(90)
	for peer in peers:
		assert_float(
			cube(peer, STACK.back()).global_position.y,
		).override_failure_message(
			"%s sees the stack %.2f m tall"
			% [peer.username, cube(peer, STACK.back()).global_position.y],
		).is_greater(1.0)

	face(ada, 1, Vector3.BACK)
	face(bo, 1, Vector3.LEFT)
	await game.sync_ticks(4)
	ada.simulate_action_press("grab")
	bo.simulate_action_press("grab")
	await game.sync_ticks(60)
	var winner_id := cube(host, 1).entity.controller
	var winner := ada if winner_id == ada.peer_id else bo
	var loser := bo if winner == ada else ada
	assert_int(cube(loser, 1).held_by).is_equal(winner_id)

	var thrower := winner.local_player as Avatar
	thrower.global_position = THROWER_AT
	thrower.global_basis = Basis.looking_at(Vector3.FORWARD)
	await game.sync_ticks(2)
	var struck := STACK[1]
	var rest := cube(winner, struck).global_position
	winner.simulate_action_release("grab")

	var impact := -1
	var responded := -1
	var decided := -1
	var loser_moved := -1
	var ran_ahead := false
	for tick in 90:
		await game.sync_ticks(1)
		var gap := cube(winner, 1).global_position.distance_to(
			cube(winner, struck).global_position,
		)
		if impact < 0 and gap < 0.55:
			impact = tick
		if responded < 0 and moved(winner, struck, rest):
			responded = tick
			var local := cube(winner, struck).entity
			ran_ahead = local.is_controlled_locally \
					and local.simulation.mode == NetwSimulationHandle.MODE_AUTHORITY \
					and cube(host, struck).entity.controller == 0
		if loser_moved < 0 and moved(loser, struck, rest):
			loser_moved = tick
		if decided < 0 and cube(host, struck).entity.controller == winner_id:
			decided = tick
		if responded >= 0 and decided >= 0 and loser_moved >= 0:
			break
	assert_int(impact).is_greater_equal(0)
	assert_int(responded - impact).override_failure_message(
		"the thrower's stack moved %d ticks after impact" % (responded - impact),
	).is_less_equal(1)
	assert_bool(ran_ahead).override_failure_message(
		"the thrower's stack moved only after the host decided",
	).is_true()
	assert_int(decided).is_greater(responded)
	assert_int(loser_moved).is_greater(responded)

	for _tick in 900:
		await game.sync_ticks(1)
		if claimed(peers).is_empty():
			break
	await game.sync_ticks(10)
	assert_array(claimed(peers)).override_failure_message(
		"cubes still claimed after rest %s" % [claimed(peers)],
	).is_empty()
	var worst := 0.0
	var worst_at := ""
	for n in range(1, 17):
		for peer in [ada, bo]:
			var apart := cube(peer, n).global_position.distance_to(
				cube(host, n).global_position,
			)
			if apart > worst:
				worst = apart
				worst_at = "%s:%d" % [peer.username, n]
		for peer in peers:
			assert_int(cube(peer, n).held_by).is_equal(0)
	assert_float(worst).override_failure_message(
		"%s rests %.3f m from the host" % [worst_at, worst],
	).is_less(0.05)


func test_a_holders_disconnect_drops_the_cube_on_every_peer() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	var bo := await game.add_client("bo")
	var peers: Array[NetwSceneRunner] = [host, bo]

	await grab(ada, 6, Vector3.BACK)
	(ada.local_player as Avatar).global_position += Vector3.UP * 2.0
	await game.sync_ticks(30)
	for peer in peers:
		assert_int(cube(peer, 6).held_by).is_equal(ada.peer_id)
		assert_float(cube(peer, 6).global_position.y).is_greater(2.0)

	await game.disconnect_runner(ada)
	await game.sync_ticks(90)
	for peer in peers:
		var dropped := cube(peer, 6)
		assert_int(dropped.entity.controller).is_equal(0)
		assert_int(dropped.held_by).override_failure_message(
			"%s reads held_by %d after the holder left"
			% [peer.username, dropped.held_by],
		).is_equal(0)
		assert_bool(dropped.freeze).is_false()
		assert_float(dropped.global_position.y).override_failure_message(
			"%s holds the cube at %.2f m" % [peer.username, dropped.global_position.y],
		).is_less(0.4)


func grab(peer: NetwSceneRunner, n: int, side: Vector3) -> void:
	face(peer, n, side)
	await game.sync_ticks(4)
	peer.simulate_action_press("grab")
	await game.sync_ticks(1)


func face(peer: NetwSceneRunner, n: int, side: Vector3) -> void:
	var avatar := peer.local_player as Avatar
	var at := cube(peer, n).global_position + side * 1.4
	avatar.global_position = Vector3(at.x, 0.8, at.z)
	avatar.global_basis = Basis.looking_at(-side)


func moved(peer: NetwSceneRunner, n: int, from: Vector3) -> bool:
	return cube(peer, n).global_position.distance_to(from) > 0.02


func cube(peer: NetwSceneRunner, n: int) -> PlayCube:
	return Netw.scene(peer.tree, &"Playground").root.get_node(
		NodePath("Cubes/Cube%d" % n),
	) as PlayCube


func claimed(peers: Array[NetwSceneRunner]) -> Array[String]:
	var out: Array[String] = []
	for peer in peers:
		for n in range(1, 17):
			if cube(peer, n).entity.controller != 0:
				out.append("%s:%d" % [peer.username, n])
	return out
