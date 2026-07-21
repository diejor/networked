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


# The prediction must remain a free reproduction of the authoritative solve.
# Pulling a live angular velocity toward an ack-old sample perturbs the next
# contact solve and manufactures the position error reconciliation then chases.
func test_prediction_does_not_pull_stale_angular_velocity() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(8)

	var angular: Array[float] = []
	var position: Array[float] = []
	var handle = own.entity.prediction
	handle.state_evaluated.connect(
		func(
				_recv_tick: int,
				_ack: int,
				_divergence: float,
				_corrected: bool,
		) -> void:
			if handle.last_compare_staleness != 0:
				return
			angular.append(
				handle.last_field_divergence.get(
					&"sphere_angular_velocity",
					0.0,
				),
			)
			position.append(
				handle.last_field_divergence.get(&"sphere_position", 0.0),
			)
	)

	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	await game.sync_ticks(120)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	assert_float(_median(angular)) \
			.override_failure_message(
				"stale feedback perturbed angular velocity by %.4f rad/s"
				% _median(angular),
			).is_less(0.01)
	assert_float(_median(position)).is_less(0.01)
	assert_int(handle.corrections).is_equal(0)


# A dynamic-body correction writes through PhysicsServer3D. The correction
# signal must report the intended position write even before the server syncs
# that transform back onto the RigidBody3D node, or the display cannot absorb
# the physics snap into its render offset.
func test_dynamic_position_correction_reports_display_delta() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var authority := await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(12)

	client.simulate_action_press("forward")
	await game.sync_ticks(20)
	var position_deltas: Array[Vector3] = []
	own.entity.prediction.recovered.connect(
		func(_entry: int, deltas: Dictionary, _teleported: bool, _attribution: int) -> void:
			if deltas.has(&"sphere_position"):
				position_deltas.append(deltas[&"sphere_position"])
	)

	authority.sphere_position += Vector3(0.6, 0.0, 0.0)
	var correction_start: int = own.entity.prediction.corrections
	for _frame in range(90):
		await game.sync_ticks(1)
		if own.entity.prediction.corrections > correction_start:
			break
	client.simulate_action_release("forward")

	assert_int(own.entity.prediction.corrections) \
			.override_failure_message("the forced position fork never corrected") \
			.is_greater(correction_start)
	# The car declares no island, so its divergences are out of domain and each
	# recovery re-bases the whole closure, the teleport-only scalars included.
	# What the display contract owes is the position delta of that one write.
	assert_int(position_deltas.size()) \
			.override_failure_message(
				"the dynamic correction omitted its display position delta",
			).is_greater(0)


# Returns the middle sample after sorting a detached copy.
func _median(values: Array[float]) -> float:
	if values.is_empty():
		return INF
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[sorted.size() / 2]
