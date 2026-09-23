extends NetwTestSuite

const MAIN := preload("res://examples/racing/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_a_controller_drives_its_own_car_forward() -> void:
	var host := await game.add_host("mario", false)
	await host.await_scene(&"Track", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	assert_that(host.local_player).is_equal(car)

	var start: Vector3 = car.sphere_position
	await game.sync_ticks(4)
	host.simulate_action_press("forward")
	await game.sync_ticks(90)
	host.simulate_action_release("forward")

	assert_float(car.sphere_position.distance_to(start)).is_greater(0.5)
	assert_float(absf(car.linear_speed)).is_greater(0.1)
	assert_int(car.entity.simulation.mode).override_failure_message(
		"the controller's car runs its step as the author",
	).is_equal(NetwSimulationHandle.MODE_AUTHORITY)


func test_every_view_of_a_car_follows_its_controllers_relayed_pose() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	var watcher := await game.add_client("peach", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	await watcher.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var host_view := await host.await_player(&"luigi", 2.0)
	var watcher_view := await watcher.await_player(&"luigi", 2.0)
	assert_that(client.local_player).is_equal(own)

	assert_bool(host_view.sphere.freeze).override_failure_message(
		"a car nobody here controls is a kinematic proxy on the relayed pose",
	).is_true()
	assert_bool(watcher_view.sphere.freeze).is_true()

	var start: Vector3 = own.sphere_position
	var host_start: Vector3 = host_view.display_position
	var watcher_start: Vector3 = watcher_view.display_position
	await game.sync_ticks(4)
	client.simulate_action_press("forward")
	await game.sync_ticks(90)
	client.simulate_action_release("forward")

	var travelled: float = own.sphere_position.distance_to(start)
	assert_float(travelled).is_greater(2.0)
	assert_float(
		host_view.display_position.distance_to(host_start),
	).override_failure_message(
		"the host view must follow the pose its controller relayed",
	).is_greater(travelled * 0.5)
	assert_float(
		watcher_view.display_position.distance_to(watcher_start),
	).override_failure_message(
		"a watcher view must follow the pose its controller relayed",
	).is_greater(travelled * 0.5)


func test_a_proxy_uses_relayed_turn_rate_for_its_tire_trails() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	var watcher := await game.add_client("peach", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	await watcher.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var watcher_view := await watcher.await_player(&"luigi", 2.0)
	own.entity.simulation.step = hold
	own.linear_speed = 1.0
	own.acceleration = 1.0
	own.angular_speed = 4.0
	await game.sync_ticks(4)
	watcher_view.effect_body(0.2)
	watcher_view.effect_trails()

	assert_bool(watcher_view.sphere.freeze).is_true()
	assert_float(watcher_view.angular_speed).is_equal_approx(4.0, 0.01)
	assert_bool(watcher_view.trail_left.emitting).is_true()
	assert_bool(watcher_view.trail_right.emitting).is_true()


func test_a_car_drives_on_after_the_host_takes_its_control() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var host_view := await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(4)
	assert_bool(own.sphere.freeze).is_false()
	assert_bool(host_view.sphere.freeze).is_true()

	host_view.entity.grant_control(host.peer_id)
	await game.sync_ticks(10)
	assert_bool(host_view.entity.is_controlled_locally).is_true()
	assert_bool(host_view.sphere.freeze).override_failure_message(
		"the host controls the car now, so its sphere runs there",
	).is_false()
	assert_bool(own.sphere.freeze).override_failure_message(
		"the client gave the car up, so its sphere is a proxy there",
	).is_true()

	var start: Vector3 = host_view.sphere_position
	var client_start: Vector3 = own.display_position
	host.simulate_action_press("forward")
	await game.sync_ticks(90)
	host.simulate_action_release("forward")

	var travelled: float = host_view.sphere_position.distance_to(start)
	assert_float(travelled).override_failure_message(
		"the new controller drives the car it was given",
	).is_greater(2.0)
	assert_float(own.display_position.distance_to(client_start)) \
			.override_failure_message(
				"the old controller's view follows the pose the host relays",
			).is_greater(travelled * 0.5)


func hold(_delta: float, _tick: int, _is_fresh: bool) -> void:
	pass
