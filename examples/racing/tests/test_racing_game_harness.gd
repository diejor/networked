extends NetwTestSuite
## Harness coverage for the racing example: the predicted dynamic-body car
## drives under the network tick, and a remote peer converges on the
## authoritative pose.

const MAIN := preload("res://examples/racing/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_host_car_spawns_and_drives_forward() -> void:
	var host := await game.add_host("mario", false)
	await host.await_scene(&"Track", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	assert_that(host.local_player).is_equal(car)

	var start: Vector3 = car.sphere_position
	await game.sync_ticks(4)
	host.simulate_action_press("forward")
	await game.sync_ticks(90)
	host.simulate_action_release("forward")

	var moved: float = car.sphere_position.distance_to(start)
	assert_float(moved).is_greater(0.5)
	assert_float(absf(car.linear_speed)).is_greater(0.1)


func test_remote_car_converges_on_authority() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	assert_that(client.local_player).is_equal(own)

	var start: Vector3 = own.sphere_position
	await game.sync_ticks(4)
	client.simulate_action_press("forward")
	await game.sync_ticks(90)

	# The owning client predicts its own car; the host holds authority. While the
	# car is driving their poses track within the SNAP correction band, and the
	# client's prediction has actually carried it down the track.
	var gap: float = own.sphere_position.distance_to(mirror.sphere_position)
	assert_float(gap).is_less(3.5)
	assert_float(own.sphere_position.distance_to(start)).is_greater(2.0)
	client.simulate_action_release("forward")
