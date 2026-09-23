extends NetwTestSuite

const MAIN := preload("res://examples/rocket_league/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_a_host_car_spawns_on_its_marker_and_drives_after_kickoff() -> void:
	var host := await game.add_host("mario", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	assert_that(host.local_player).is_equal(car)
	quiet_ai(host)

	await game.sync_ticks(4)
	assert_float(car.car_position.distance_to(car.marker.global_position)) \
			.override_failure_message(
				"a car must sit on its own kickoff marker before kickoff",
			).is_less(1.0)

	await await_kickoff(car)
	quiet_ai(host)
	var start: Vector3 = car.car_position
	var facing: Vector3 = Basis(car.car_rotation).z
	host.simulate_action_press("forward")
	await game.sync_ticks(90)
	host.simulate_action_release("forward")
	assert_float(car.car_position.distance_to(start)).is_greater(0.5)

	assert_float((car.car_position - start).dot(facing)) \
			.override_failure_message(
				"the accelerate action must move a car along its own facing",
			).is_greater(0.5)


func test_every_peer_holds_the_other_peers_car() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var host_view := await host.await_player(&"luigi", 2.0)
	var mario_on_client := await client.await_player(&"mario", 2.0)
	assert_that(client.local_player).is_equal(own)
	assert_int(own.entity.prediction.archetype).is_equal(
		NetwPredict.ARCHETYPE_SOLVER_BODY,
	)

	await await_kickoff(own)
	quiet_ai(host)
	quiet_ai(client)
	client.simulate_action_press("forward")
	await game.sync_ticks(90)
	client.simulate_action_release("forward")

	assert_float(own.car_position.distance_to(host_view.car_position)) \
			.override_failure_message(
				"the host's view of a client car must track the client's own",
			).is_less(3.5)
	assert_bool(is_instance_valid(mario_on_client)).is_true()


func test_a_goal_bumps_the_score_on_every_peer_and_queues_a_kickoff() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var server_game := rocket_game(host)
	var client_game := rocket_game(client)
	var ball := arena_ball(host)
	var car := await host.await_player(&"mario", 2.0)
	await game.sync_ticks(4)

	await await_kickoff(car)
	quiet_ai(host)
	quiet_ai(client)
	var before: int = server_game.score_red
	ball.ball_position = goal_position(host, "Goal0")
	ball.ball_linear_velocity = Vector3.ZERO
	for _tick in 120:
		await game.sync_ticks(1)
		if server_game.score_red > before:
			break

	assert_int(server_game.score_red).override_failure_message(
		"the ball resting in a goal must be ruled a goal exactly once",
	).is_equal(before + 1)
	assert_int(server_game.kickoff_tick).is_greater(0)
	await game.sync_ticks(20)
	assert_int(client_game.score_red).override_failure_message(
		"every peer reads the score off the same column",
	).is_equal(server_game.score_red)


func test_cars_hold_their_markers_and_the_ball_returns_to_its_spot() -> void:
	var host := await game.add_host("mario", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	var ball := arena_ball(host)
	var server_game := rocket_game(host)
	quiet_ai(host)
	await game.sync_ticks(4)

	assert_int(server_game.kickoff_tick).override_failure_message(
		"a join queues a kickoff",
	).is_greater(0)
	host.simulate_action_press("forward")
	await game.sync_ticks(30)
	host.simulate_action_release("forward")
	assert_float(car.car_position.distance_to(car.marker.global_position)) \
			.override_failure_message(
				"input must not move a car that is held for the kickoff",
			).is_less(1.0)

	await await_kickoff(car)
	assert_float(ball.ball_position.distance_to(RocketBall.STARTING_POSITION)) \
			.override_failure_message(
				"the reset must put the ball back on its spot before play",
			).is_less(1.0)


func test_the_ball_holds_one_route_on_every_peer_and_keeps_its_own_body() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var host_ball := arena_ball(host)
	var client_ball := arena_ball(client)
	var car := await host.await_player(&"mario", 2.0)
	await await_kickoff(car)
	quiet_ai(host)
	quiet_ai(client)
	await game.sync_ticks(10)

	var host_route: int = host_ball.entity.route
	assert_int(host_route).override_failure_message(
		"the ball is an entity of its own, so the host routes it",
	).is_greater(0)
	assert_int(client_ball.entity.route).override_failure_message(
		"the client holds the ball under the route the host published, "
		+ "and read %d against the host's %d"
		% [client_ball.entity.route, host_route],
	).is_equal(host_route)

	assert_int(host_ball.entity.prediction.archetype).override_failure_message(
		"the ball keeps its own prediction body on every peer",
	).is_equal(NetwPredict.ARCHETYPE_SOLVER_BODY)
	assert_int(client_ball.entity.prediction.archetype).is_equal(
		NetwPredict.ARCHETYPE_SOLVER_BODY,
	)
	assert_that(client_ball.entity.scene.root).override_failure_message(
		"the client's ball answers the arena its route was published under",
	).is_equal(arena_level(client))


func test_a_car_drives_itself_toward_the_ball_with_no_local_input() -> void:
	var host := await game.add_host("mario", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	assert_bool(car.ai_enabled).override_failure_message(
		"a car drives itself until its controller takes the wheel",
	).is_true()

	await await_kickoff(car)
	var ball := arena_ball(host)
	var before: float = car.car_position.distance_to(ball.ball_position)
	await game.sync_ticks(120)
	var after: float = car.car_position.distance_to(ball.ball_position)
	assert_float(after).override_failure_message(
		"the AI closed %.2fm to %.2fm on the ball" % [before, after],
	).is_less(before)


func test_a_clean_client_drive_opens_no_correction() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	await host.await_player(&"luigi", 2.0)

	await await_kickoff(own)
	quiet_ai(host)
	quiet_ai(client)
	var at_kickoff: int = own.entity.prediction.stats.corrections
	await game.sync_ticks(16)
	var corrections: int = own.entity.prediction.stats.corrections
	client.simulate_action_press("forward")
	await game.sync_ticks(120)
	client.simulate_action_release("forward")

	var opened: int = own.entity.prediction.stats.corrections
	assert_int(opened) \
			.override_failure_message(
				"a straight drive with no contact must reconcile clean: "
				+ "%d at kickoff, %d after the settle, %d after the drive"
				% [at_kickoff, corrections, opened],
			).is_equal(corrections)


func test_the_lobby_lists_every_waiting_player_before_the_match_opens() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Lobby", 2.0)
	await client.await_scene(&"Lobby", 2.0)
	await game.sync_ticks(4)

	var roster := in_lobby(host).member_list
	assert_int(roster.item_count).override_failure_message(
		"the lobby roster carries every seated player",
	).is_equal(2)
	assert_bool(in_lobby(client).start_btn.visible).override_failure_message(
		"only the host starts the match",
	).is_false()

	in_lobby(host).on_start_pressed()
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	assert_that(await client.await_player(&"luigi", 2.0)).is_not_null()


func test_a_clients_ball_responds_in_the_frame_its_car_touches_it() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var host_ball := arena_ball(host)
	var client_ball := arena_ball(client)
	await await_kickoff(own)
	quiet_ai(host)
	quiet_ai(client)
	await game.sync_ticks(4)
	assert_int(client_ball.entity.simulation.mode).override_failure_message(
		"the client's car selects the ball, so the client runs it",
	).is_equal(NetwSimulationHandle.MODE_ACTIVE)

	var ahead: Vector3 = own.car_position + Basis(own.car_rotation).z * 5.0
	ahead.y = RocketBall.STARTING_POSITION.y
	host_ball.ball_position = ahead
	host_ball.ball_linear_velocity = Vector3.ZERO
	var still := 0
	for _tick in 400:
		await game.sync_ticks(1)
		var resting: bool = host_ball.ball_linear_velocity.length() < 0.05
		still = still + 1 if resting else 0
		if still >= 10:
			break
	game.degrade(client).inbound().latency_ms(300.0)
	await game.sync_ticks(20)

	client.simulate_action_press("forward")
	var touched := -1
	var moved := -1
	for tick in 240:
		await game.sync_ticks(1)
		if touched < 0 and own.get_colliding_bodies().has(client_ball):
			touched = tick
		var drift := client_ball.ball_linear_velocity * Vector3(1.0, 0.0, 1.0)
		if touched >= 0 and drift.length() > 2.0:
			moved = tick
			break
	client.simulate_action_release("forward")

	assert_int(touched).override_failure_message(
		"the client's car never reached the ball",
	).is_greater_equal(0)
	assert_int(moved - touched).override_failure_message(
		"the client's ball moved %d ticks after its car touched it"
		% (moved - touched),
	).is_between(0, 2)


func test_a_clients_hit_on_a_bouncing_ball_takes_effect_at_once() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var host_ball := arena_ball(host)
	var client_ball := arena_ball(client)
	await await_kickoff(own)
	quiet_ai(host)
	quiet_ai(client)
	await game.sync_ticks(4)
	assert_int(client_ball.entity.simulation.mode).override_failure_message(
		"the client's car selects the ball, so the client runs it",
	).is_equal(NetwSimulationHandle.MODE_ACTIVE)

	var ahead: Vector3 = own.car_position + Basis(own.car_rotation).z * 5.0
	ahead.y = RocketBall.STARTING_POSITION.y
	host_ball.ball_position = ahead
	host_ball.ball_linear_velocity = Vector3.ZERO
	game.degrade(client).inbound().latency_ms(300.0)
	for _tick in 20:
		await game.sync_ticks(1)
		bounce(host_ball)

	client.simulate_action_press("forward")
	var touched := -1
	var moved := -1
	var server_speed := 0.0
	for tick in 240:
		if touched < 0:
			bounce(host_ball)
			var speed := host_ball.ball_linear_velocity.length()
			server_speed = maxf(server_speed, speed)
		await game.sync_ticks(1)
		if touched < 0 and own.get_colliding_bodies().has(client_ball):
			touched = tick
		var drift := client_ball.ball_linear_velocity * Vector3(1.0, 0.0, 1.0)
		if touched >= 0 and drift.length() > 2.0:
			moved = tick
			break
	client.simulate_action_release("forward")

	assert_int(touched).override_failure_message(
		"the client's car never reached the ball",
	).is_greater_equal(0)
	assert_float(server_speed).override_failure_message(
		"the server's ball rested while the client's car drove at it",
	).is_greater(0.5)
	assert_int(moved - touched).override_failure_message(
		"the client's ball moved %d ticks after its car touched it"
		% (moved - touched),
	).is_between(0, 2)


func test_a_clients_ball_visibly_rotates_with_its_state() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	await client.await_scene(&"Arena", 2.0)
	var host_ball := arena_ball(host)
	var client_ball := arena_ball(client)
	var car := await host.await_player(&"mario", 2.0)
	await await_kickoff(car)
	quiet_ai(host)

	host_ball.ball_angular_velocity = Vector3(0.0, 6.0, 0.0)
	var before := client_ball.transform.basis.get_rotation_quaternion()
	await game.sync_ticks(60)
	var after := client_ball.transform.basis.get_rotation_quaternion()
	assert_float(after.angle_to(before)) \
			.override_failure_message(
				"a client's replicated ball spun but its drawn transform "
				+ "did not follow",
			).is_greater(0.05)


func test_a_late_join_gets_a_car_on_the_other_team() -> void:
	var host := await game.add_host("mario", false)
	await begin_match(host)
	await host.await_scene(&"Arena", 2.0)
	var first := await host.await_player(&"mario", 2.0) as RocketCar
	assert_int(first.team).is_equal(0)

	var client := await game.add_client("luigi", false)
	await client.await_scene(&"Arena", 2.0)
	var late := await client.await_player(&"luigi", 2.0) as RocketCar
	assert_that(client.local_player).override_failure_message(
		"a peer joining a running match is given a car of its own",
	).is_equal(late)
	assert_int(late.team).override_failure_message(
		"a late join takes the next grid slot, so it lands on the other team",
	).is_equal(1)
	assert_that(await host.await_player(&"luigi", 2.0)).is_not_null()


func bounce(ball: RocketBall) -> void:
	if ball.ball_position.y < 1.1 and ball.ball_linear_velocity.y <= 0.0:
		ball.ball_linear_velocity = Vector3(0.0, 2.0, 0.0)


func begin_match(host: NetwSceneRunner) -> void:
	await host.await_scene(&"Lobby", 2.0)
	in_lobby(host).on_start_pressed()


func in_lobby(peer: NetwSceneRunner) -> InLobby:
	return Netw.scene(peer.tree, &"Lobby").root.find_child(
		"InLobby",
		true,
		false,
	) as InLobby


func quiet_ai(peer: NetwSceneRunner) -> void:
	for child in arena_level(peer).get_node(^"Players").get_children():
		if child is RocketCar:
			(child as RocketCar).ai_enabled = false


func await_kickoff(car: RocketCar) -> void:
	for _tick in 400:
		var tick: int = Netw.clock(car).tick
		if car.game.kickoff_tick > 0 and tick >= car.game.kickoff_tick:
			return
		await game.sync_ticks(1)


func arena_level(peer: NetwSceneRunner) -> Node:
	return Netw.scene(peer.tree, &"Arena").root


func arena_ball(peer: NetwSceneRunner) -> RocketBall:
	return arena_level(peer).get_node(^"ball")


func rocket_game(peer: NetwSceneRunner) -> RocketGame:
	return arena_level(peer).get_node(^"game")


func goal_position(peer: NetwSceneRunner, named: String) -> Vector3:
	return (arena_level(peer).get_node(NodePath("field/%s" % named)) as Node3D) \
			.global_position
