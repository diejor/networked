extends NetwTestSuite
## Measures the B5 present-time transport candidates on the real racing body.
##
## These cases isolate the owning and authoritative copies from dynamic peers,
## inject one historical error while commands stay active, and report whether
## natural recurrence or a one-shot transported pose closes it in clear space.

const MAIN := preload("res://examples/racing/main.tscn")
const Handle := NetwLagCompensationInterface.PredictionHandle
const HORIZON_TICKS := 8

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func _setup_drive(turning: bool = false) -> Array:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var authority := await host.await_player(&"luigi", 2.0)
	var host_car := await client.await_player(&"mario", 2.0)
	var host_authority := await host.await_player(&"mario", 2.0)
	for car in [own, authority, host_car, host_authority]:
		car.sphere.collision_mask = 1
	own.entity.prediction.recovery_policy = Handle.RecoveryPolicy.OBSERVE
	await game.sync_ticks(16)
	client.simulate_action_press("forward")
	if turning:
		client.simulate_action_press("right")
	await game.sync_ticks(30)
	return [client, own, authority]


func _momentum_delta(own: Node, authority: Node) -> Array[Vector3]:
	return [
		own.sphere_linear_velocity - authority.sphere_linear_velocity,
		own.sphere_angular_velocity - authority.sphere_angular_velocity,
	]


func _momentum_delta_error(
		own: Node,
		authority: Node,
		baseline: Array[Vector3],
) -> Vector2:
	var current := _momentum_delta(own, authority)
	return Vector2(
		current[0].distance_to(baseline[0]),
		current[1].distance_to(baseline[1]),
	)


func test_momentum_marks_are_actionable_but_withheld_below_teleport() -> void:
	var parts := await _setup_drive()
	var own: Node = parts[1]
	var binding = own.entity.state_binding
	for field in [
		&"sphere_linear_velocity",
		&"sphere_angular_velocity",
	]:
		assert_bool(binding.reconcile_only_of(field)).is_false()
		assert_bool(binding.teleport_only_of(field)).is_true()
		assert_float(binding.epsilon_override_of(field)).is_greater(0.0)


func test_position_offset_does_not_naturally_close() -> void:
	var parts := await _setup_drive()
	var own: Node = parts[1]
	var authority: Node = parts[2]
	var baseline: Vector3 = own.sphere_position - authority.sphere_position
	own.sphere_position += Vector3(0.4, 0.0, 0.0)
	await game.sync_ticks(1)
	var opened: Vector3 = own.sphere_position \
			- authority.sphere_position - baseline
	await game.sync_ticks(HORIZON_TICKS)
	var settled: Vector3 = own.sphere_position \
			- authority.sphere_position - baseline
	print(
		"[b5:natural-position] opened=%.4f settled=%.4f" % [
			opened.length(),
			settled.length(),
		],
	)
	assert_float(opened.length()).is_greater(0.2)
	assert_float(settled.length()).is_greater(opened.length() * 0.5)


func test_heading_offset_under_steering_forks_momentum() -> void:
	var parts := await _setup_drive(true)
	var own: Node = parts[1]
	var authority: Node = parts[2]
	var baseline_heading := angle_difference(own.heading, authority.heading)
	var baseline_momentum := _momentum_delta(own, authority)
	own.heading += 0.2
	await game.sync_ticks(1)
	var heading_opened := absf(
		angle_difference(own.heading, authority.heading) - baseline_heading,
	)
	await game.sync_ticks(HORIZON_TICKS)
	var heading_settled := absf(
		angle_difference(own.heading, authority.heading) - baseline_heading,
	)
	var momentum_after := _momentum_delta_error(
		own,
		authority,
		baseline_momentum,
	)
	print(
		"[b5:natural-heading] heading=%.4f->%.4f momentum=%s"
		% [heading_opened, heading_settled, momentum_after],
	)
	assert_float(heading_opened).is_greater(0.1)
	assert_float(heading_settled + momentum_after.x + momentum_after.y) \
			.is_greater(0.01)


func test_velocity_persists_but_the_drive_overwrites_the_latch() -> void:
	var parts := await _setup_drive(true)
	var own: Node = parts[1]
	var authority: Node = parts[2]
	var baseline_velocity: Vector3 = own.sphere_linear_velocity \
			- authority.sphere_linear_velocity
	var contact_count: int = own.sphere.get_colliding_bodies().size()
	own.sphere_linear_velocity += Vector3(0.6, 0.0, 0.0)
	own.steer_direction = -authority.steer_direction
	var velocity_injected: float = (
			own.sphere_linear_velocity
			- authority.sphere_linear_velocity
			- baseline_velocity
	).length()
	var latch_injected := absf(
		own.steer_direction - authority.steer_direction,
	)
	await game.sync_ticks(1)
	var latch_after_one := absf(
		own.steer_direction - authority.steer_direction,
	)
	await game.sync_ticks(HORIZON_TICKS)
	var velocity_after: float = (
			own.sphere_linear_velocity
			- authority.sphere_linear_velocity
			- baseline_velocity
	).length()
	var contact_after: int = own.sphere.get_colliding_bodies().size()
	print(
		(
				"[b5:non-pose] velocity=%.4f->%.4f latch=%.1f->%.1f "
				+ "contacts=%d->%d"
		) % [
			velocity_injected,
			velocity_after,
			latch_injected,
			latch_after_one,
			contact_count,
			contact_after,
		],
	)
	assert_float(velocity_injected).is_greater(0.5)
	assert_float(latch_injected).is_greater(1.0)
	assert_float(latch_after_one).is_less(0.1)
	assert_float(velocity_after).is_greater(0.01)


func test_pose_transport_preserves_the_newer_straight_progress() -> void:
	var parts := await _setup_drive()
	var own: Node = parts[1]
	var authority: Node = parts[2]
	var baseline_delta: Vector3 = authority.sphere_position \
			- own.sphere_position
	own.sphere_position += Vector3(0.4, 0.0, 0.0)
	await game.sync_ticks(1)
	var aligned_delta: Vector3 = authority.sphere_position \
			- own.sphere_position - baseline_delta
	var basis_live: Vector3 = own.sphere_position
	await game.sync_ticks(HORIZON_TICKS)
	var before: Vector3 = own.sphere_position - authority.sphere_position \
			+ baseline_delta
	var live_before: Vector3 = own.sphere_position
	var newer_progress: Vector3 = live_before - basis_live
	var expected: Vector3 = live_before + aligned_delta
	var contacts_before: int = own.sphere.get_colliding_bodies().size()
	own.sphere_position += aligned_delta
	var staged_transform := PhysicsServer3D.body_get_state(
		own.sphere.get_rid(),
		PhysicsServer3D.BODY_STATE_TRANSFORM,
	) as Transform3D
	var staged_local: Vector3 = own.global_transform.affine_inverse() \
			* staged_transform.origin
	var composition_error: float = staged_local.distance_to(expected)
	await game.sync_ticks(1)
	var after: Vector3 = own.sphere_position - authority.sphere_position \
			+ baseline_delta
	var contacts_after: int = own.sphere.get_colliding_bodies().size()
	print(
		(
				"[b5:transport-position] error=%.4f->%.4f progress=%.4f "
				+ "composition=%.6f contacts=%d->%d sleeping=%s"
		) % [
			before.length(),
			after.length(),
			newer_progress.length(),
			composition_error,
			contacts_before,
			contacts_after,
			own.sphere.sleeping,
		],
	)
	assert_float(after.length()).is_less(before.length())
	assert_float(newer_progress.length()).is_greater(0.01)
	assert_float(composition_error).is_less(0.001)
	assert_int(contacts_after).is_equal(contacts_before)
	assert_bool(own.sphere.sleeping).is_false()


func test_pose_transport_under_steering_leaves_a_momentum_fork() -> void:
	var parts := await _setup_drive(true)
	var own: Node = parts[1]
	var authority: Node = parts[2]
	var baseline_position: Vector3 = authority.sphere_position \
			- own.sphere_position
	var baseline_heading := angle_difference(own.heading, authority.heading)
	var baseline_momentum := _momentum_delta(own, authority)
	own.sphere_position += Vector3(0.25, 0.0, 0.0)
	own.heading += 0.2
	await game.sync_ticks(1)
	var position_delta: Vector3 = authority.sphere_position \
			- own.sphere_position - baseline_position
	var heading_delta := angle_difference(
		own.heading,
		authority.heading,
	) - baseline_heading
	await game.sync_ticks(HORIZON_TICKS)
	own.sphere_position += position_delta
	own.heading += heading_delta
	await game.sync_ticks(1)
	var position_after: Vector3 = own.sphere_position \
			- authority.sphere_position + baseline_position
	var heading_after := absf(
		angle_difference(own.heading, authority.heading) - baseline_heading,
	)
	var momentum_after := _momentum_delta_error(
		own,
		authority,
		baseline_momentum,
	)
	print(
		"[b5:transport-heading] pose=(%.4f, %.4f) momentum=%s"
		% [position_after.length(), heading_after, momentum_after],
	)
	assert_float(
		position_after.length() + heading_after \
				+ momentum_after.x + momentum_after.y,
	) \
			.override_failure_message(
				"the steering candidate unexpectedly erased every injected error",
			).is_greater(0.001)


func test_body_motion_query_distinguishes_clear_and_blocked_corridors() -> void:
	var parts := await _setup_drive()
	var own: Node = parts[1]
	var clear := PhysicsTestMotionParameters3D.new()
	clear.from = own.sphere.global_transform
	clear.motion = Vector3(0.1, 0.0, 0.0)
	clear.margin = 0.001
	var blocked := PhysicsTestMotionParameters3D.new()
	blocked.from = own.sphere.global_transform
	blocked.motion = Vector3(100.0, 0.0, 0.0)
	blocked.margin = 0.001
	var clear_hit := PhysicsServer3D.body_test_motion(
		own.sphere.get_rid(),
		clear,
		PhysicsTestMotionResult3D.new(),
	)
	var blocked_hit := PhysicsServer3D.body_test_motion(
		own.sphere.get_rid(),
		blocked,
		PhysicsTestMotionResult3D.new(),
	)
	print("[b5:corridor] clear=%s blocked=%s" % [clear_hit, blocked_hit])
	assert_bool(clear_hit).is_false()
	assert_bool(blocked_hit).is_true()
