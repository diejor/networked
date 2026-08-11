extends NetwTestSuite
## Harness coverage for the racing example: the predicted dynamic-body car
## drives under the network tick, and a remote peer converges on the
## authoritative pose.

const MAIN := preload("res://examples/racing/main.tscn")
const SIMULATE_NEAREST_VAR := "NETW_RACING_SIMULATE_NEAREST"

var game: NetwGameHarness


func before_test() -> void:
	OS.unset_environment(SIMULATE_NEAREST_VAR)
	game = make_game_harness(MAIN)
	await game.setup()


func after_test() -> void:
	OS.unset_environment(SIMULATE_NEAREST_VAR)


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


func test_every_view_of_a_remote_car_converges_on_authority() -> void:
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
	assert_int(own.entity.prediction.archetype).is_equal(
		NetwPredict.Archetype.SOLVER_BODY,
	)
	assert_int(own.entity.prediction.breach_response).is_equal(
		NetwPredict.BreachResponse.DEMOTE,
	)

	var start: Vector3 = own.sphere_position
	await game.sync_ticks(4)
	client.simulate_action_press("forward")
	await game.sync_ticks(90)

	# The owning client predicts its own car; the host holds authority. While the
	# car is driving their poses track within the SNAP correction band, and the
	# client's prediction has actually carried it down the track.
	var gap: float = own.sphere_position.distance_to(host_view.sphere_position)
	assert_float(gap).is_less(3.5)
	assert_float(own.sphere_position.distance_to(start)).is_greater(2.0)
	assert_float(
		host_view.sphere_position.distance_to(host_view.display_position),
	).override_failure_message(
		"the host display must follow the client car it simulates",
	).is_less(3.0)
	assert_float(
		watcher_view.sphere_position.distance_to(watcher_view.display_position),
	).override_failure_message(
		"a remote watcher display must follow the received car stream",
	).is_less(3.0)
	client.simulate_action_release("forward")


@warning_ignore("unused_parameter")
func test_remote_visual_stays_attached_through_a_wall_contact(
		do_skip = OS.get_environment("NETW_MARGINAL").is_empty(),
		skip_reason = "Load-marginal probe, not a law. It fails on an unchanged tree about one run in three. Set NETW_MARGINAL=1 to run it.",
) -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var authority := await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(4)

	var max_authority_visual_gap := 0.0
	var entered_display_mode := false
	client.simulate_action_press("forward")
	for _tick in 720:
		await game.sync_ticks(1)
		max_authority_visual_gap = maxf(
			max_authority_visual_gap,
			authority.sphere_position.distance_to(
				authority.vehicle_model.position + Vector3(0, 0.65, 0),
			),
		)
		entered_display_mode = entered_display_mode or (
			own.entity.prediction.sim_mode
			== NetwPredict.SimMode.DISPLAY
		)
	client.simulate_action_release("forward")

	assert_bool(entered_display_mode).override_failure_message(
		"wall recovery must not freeze the owning car into delayed display",
	).is_false()
	assert_float(max_authority_visual_gap).override_failure_message(
		"the authority visual detached %.3fm from the remote car collider"
		% max_authority_visual_gap,
	).is_less(0.5)


func test_scene_toggle_simulates_the_nearest_remote_car() -> void:
	OS.set_environment(SIMULATE_NEAREST_VAR, "1")
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var remote := await client.await_player(&"mario", 2.0)
	await game.sync_ticks(4)

	assert_int(own.entity.prediction.sim_mode).is_equal(
		NetwPredict.SimMode.SPECULATIVE,
	)
	assert_int(remote.entity.prediction.input_source).is_equal(
		NetwPredict.InputSource.PREDICTED,
	)
	assert_int(remote.entity.prediction.sim_mode).is_equal(
		NetwPredict.SimMode.SPECULATIVE,
	)
	assert_bool(remote.sphere.freeze).override_failure_message(
		"the published simulated cell must unfreeze racing's child body",
	).is_false()


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
				_diverged: bool,
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
	assert_int(handle.stats.corrections).is_equal(0)


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
	var correction_start: int = own.entity.prediction.stats.corrections
	for _frame in range(90):
		await game.sync_ticks(1)
		if own.entity.prediction.stats.corrections > correction_start:
			break
	client.simulate_action_release("forward")

	assert_int(own.entity.prediction.stats.corrections) \
			.override_failure_message("the forced position fork never corrected") \
			.is_greater(correction_start)
	# The car declares no island, so its divergences are out of domain and each
	# recovery re-bases the whole closure, the teleport-only scalars included.
	# What the display contract owes is the position delta of that one write.
	assert_int(position_deltas.size()) \
			.override_failure_message(
				"the dynamic correction omitted its display position delta",
			).is_greater(0)


# The reachability report, read on the real car rather than on a rig, because what
# it has to be right about is a game's actual declarations.
#
# Racing is the entity that taught the campaign why this is needed: its two solver
# velocities are teleport_only() and free to trigger, so they demand recoveries
# that no sub-teleport restore may write and are answered by writing other fields
# instead -- 1393 triggers against 50 writes in one capture, none of which any
# single tick could show. And its breach response is DEMOTE, which since D5 no
# archetype sets, so the report has to name the car itself as the source.
func test_the_reachability_report_names_what_the_car_cannot_repair() -> void:
	var host := await game.add_host("mario", false)
	await host.await_scene(&"Track", 2.0)
	var car := await host.await_player(&"mario", 2.0)
	await game.sync_ticks(4)

	var report: Dictionary = car.entity.prediction.reachability()
	assert_bool(report.is_empty()).override_failure_message(
		"a wired car must be able to answer for its own declarations",
	).is_false()

	# The two momentum fields: reachable by nothing below the teleport tier, and
	# with no forward model to advance the one write that does reach them.
	for key: StringName in [&"sphere_linear_velocity", &"sphere_angular_velocity"]:
		var row: Dictionary = report[&"fields"][key]
		assert_bool(bool(row[&"triggers"])).override_failure_message(
			"%s is causal and not reconcile_only, so it still asks" % key,
		).is_true()
		assert_array(row[&"operators"]).override_failure_message(
			"%s must show the full closure as its only operator" % key,
		).is_equal(["full_closure"])
		assert_str(String(row[&"forward_model"][&"kind"])).is_equal("none")

	# The pose fields, by contrast, declare a channel that IS live.
	for key: StringName in [&"sphere_position", &"heading"]:
		var model: Dictionary = report[&"fields"][key][&"forward_model"]
		assert_str(String(model[&"kind"])).is_equal("channel")
		assert_bool(bool(model[&"live"])).override_failure_message(
			"%s declares a replicated derivative this set carries" % key,
		).is_true()
		assert_bool(bool(report[&"fields"][key][&"in_tier"])).is_true()

	# The finding the wiring report says out loud, asserted on the report rather
	# than on log text.
	var unrepairable := PackedStringArray()
	for finding: Dictionary in report[&"findings"]:
		if String(finding[&"code"]) == "unrepairable":
			for f in finding[&"fields"]:
				unrepairable.append(String(f))
	assert_array(unrepairable).contains([
		"sphere_linear_velocity", "sphere_angular_velocity",
	])

	# D5: the car names its own breach response, and the report says who did.
	assert_str(String(report[&"breach"][&"response"])).is_equal("DEMOTE")
	assert_str(String(report[&"breach"][&"declared_by"])) \
			.override_failure_message(
				"a preset naming this is exactly what D5 removed",
			).is_equal("code")


# Returns the middle sample after sorting a detached copy.
func _median(values: Array[float]) -> float:
	if values.is_empty():
		return INF
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[sorted.size() / 2]
