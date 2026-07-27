## Laws for holding a physics space, the lever the simulation gate is built on.
##
## A transition is a fixed quantum of simulated time, and the only way a peer
## with spare frames can hold that quantum is to decline the frame's solve. The
## whole design rests on one property: a held frame must be a pure skip, not a
## step of zero length and not a step at all. Two worlds given the same solves in
## the same order must land in the same place whatever wall-clock frames those
## solves were spread across.
##
## This is the probe that has to pass before a gate is built, so it is written as
## laws rather than as a report.
class_name TestSimulationGateSemantics
extends NetwTestSuite

const RADIUS := 0.5
const MASS := 1.0
const GRAVITY_SCALE := 5.0
const LINEAR_DAMP := 1.5
const ANGULAR_DAMP := 4.0
const FRICTION := 1.0

# Solves each world performs. Long enough that a per-step difference would
# accumulate somewhere visible rather than hiding in the last digit.
const _SOLVES := 120


class GatedWorld:
	extends RefCounted

	var viewport: SubViewport
	var body: RigidBody3D
	var space: RID


	func hold(value: bool) -> void:
		PhysicsServer3D.space_set_active(space, not value)


func _world() -> GatedWorld:
	var out := GatedWorld.new()
	out.viewport = SubViewport.new()
	out.viewport.own_world_3d = true
	out.viewport.world_3d = World3D.new()
	add_child(out.viewport)
	auto_free(out.viewport)

	var ground := StaticBody3D.new()
	var ground_shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(200.0, 1.0, 200.0)
	ground_shape.shape = box
	ground.add_child(ground_shape)
	ground.position = Vector3(0.0, -0.5, 0.0)
	var ground_material := PhysicsMaterial.new()
	ground_material.friction = FRICTION
	ground_material.rough = true
	ground.physics_material_override = ground_material
	out.viewport.add_child(ground)

	out.body = RigidBody3D.new()
	var shape := CollisionShape3D.new()
	var ball := SphereShape3D.new()
	ball.radius = RADIUS
	shape.shape = ball
	out.body.add_child(shape)
	out.body.mass = MASS
	out.body.gravity_scale = GRAVITY_SCALE
	out.body.linear_damp = LINEAR_DAMP
	out.body.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	out.body.angular_damp = ANGULAR_DAMP
	var material := PhysicsMaterial.new()
	material.friction = FRICTION
	material.rough = true
	out.body.physics_material_override = material
	out.body.position = Vector3(0.0, RADIUS, 0.0)
	out.viewport.add_child(out.body)

	# Read off the body, never off the viewport: a viewport's declared world and
	# the world its children actually resolve into are two different questions,
	# and only the second one names the space that steps this body.
	out.space = out.body.get_world_3d().space
	return out


# The drive, keyed on the solve index rather than the frame, so two worlds given
# the same solves receive the same impulses whatever frames they landed on.
func _drive(solve: int, delta: float) -> Vector3:
	var turn := sin(float(solve) * 0.05)
	return Vector3(turn, 0.0, 1.0).normalized() * 60.0 * delta


# THE law. A world whose space is held on one frame in [param hold_period] must
# reach the same state as one that never held, given the same solves in the same
# order. A held frame that ran a zero-length step, or that let the solver advance
# anything at all, would separate the two.
func _assert_holding_is_a_pure_skip(hold_period: int) -> void:
	var open := _world()
	var gated := _world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	var open_solves := 0
	var gated_solves := 0
	var frames := 0
	while gated_solves < _SOLVES and frames < _SOLVES * 4:
		var open_steps := open_solves < _SOLVES
		var gated_steps := gated_solves < _SOLVES \
				and frames % hold_period != 0
		if open_steps:
			open.body.angular_velocity += _drive(open_solves, delta)
		if gated_steps:
			gated.body.angular_velocity += _drive(gated_solves, delta)
		open.hold(not open_steps)
		gated.hold(not gated_steps)
		await get_tree().physics_frame
		if open_steps:
			open_solves += 1
		if gated_steps:
			gated_solves += 1
		frames += 1
	open.hold(false)
	gated.hold(false)

	var position_gap := open.body.position.distance_to(gated.body.position)
	var linear_gap := (
			open.body.linear_velocity - gated.body.linear_velocity
	).length()
	var angular_gap := (
			open.body.angular_velocity - gated.body.angular_velocity
	).length()
	print(
		"[gate] hold 1-in-%d over %d frames: position=%.9f linear=%.9f "
		% [hold_period, frames, position_gap, linear_gap]
		+ "angular=%.9f" % [angular_gap],
	)

	assert_int(gated_solves).override_failure_message(
		"the gated world never reached its solve count, so the arm compares "
		+ "two different runs",
	).is_equal(_SOLVES)
	assert_float(position_gap).override_failure_message(
		(
				"holding a space must skip the solve, not shorten it: %d solves "
				+ "spread over %d frames landed %.9f m from the same %d solves "
				+ "run back to back"
		) % [_SOLVES, frames, position_gap, _SOLVES],
	).is_less(0.000001)
	assert_float(linear_gap).is_less(0.000001)
	assert_float(angular_gap).is_less(0.000001)


func test_holding_one_frame_in_three_is_a_pure_skip() -> void:
	await _assert_holding_is_a_pure_skip(3)


# The measured rendered ratio was one spare frame in seven, so the arm the gate
# will actually run carries that density.
func test_holding_one_frame_in_seven_is_a_pure_skip() -> void:
	await _assert_holding_is_a_pure_skip(7)


# A recovery writes to the body outside the solve, and the gate must not swallow
# the write. Whatever a held frame does with a queued state change, the value
# has to be the one the next solve starts from.
func test_a_write_during_a_held_frame_survives_the_hold() -> void:
	var world := _world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	world.hold(true)
	world.body.position = Vector3(3.0, RADIUS, -2.0)
	world.body.linear_velocity = Vector3.ZERO
	world.body.angular_velocity = Vector3.ZERO
	for _frame in 5:
		await get_tree().physics_frame

	assert_vector(world.body.position).override_failure_message(
		"a body written while its space was held must hold the written pose",
	).is_equal_approx(Vector3(3.0, RADIUS, -2.0), Vector3.ONE * 0.001)

	world.hold(false)
	await get_tree().physics_frame

	assert_vector(world.body.position).override_failure_message(
		"the first solve after a hold must start from the written pose",
	).is_equal_approx(Vector3(3.0, RADIUS, -2.0), Vector3.ONE * 0.05)


# The lever itself, on a body that would otherwise be moving. A settled body
# proves nothing here, because a settled body does not move whether the space
# steps or not.
func test_a_hold_stops_a_body_that_would_have_moved() -> void:
	var open := _world()
	var held := _world()
	await get_tree().physics_frame
	assert_bool(open.space != held.space).override_failure_message(
		"the two worlds share a space, so the arm compares one body to itself",
	).is_true()

	open.body.angular_velocity = Vector3(0.0, 0.0, 6.0)
	held.body.angular_velocity = Vector3(0.0, 0.0, 6.0)
	await get_tree().physics_frame
	var open_start := open.body.position
	var held_start := held.body.position

	held.hold(true)
	for _frame in 30:
		await get_tree().physics_frame
	var open_travel := open_start.distance_to(open.body.position)
	var held_travel := held_start.distance_to(held.body.position)
	held.hold(false)

	print(
		"[gate] over 30 held frames: open moved %.6f, held moved %.6f"
		% [open_travel, held_travel],
	)
	assert_float(open_travel).override_failure_message(
		"the arm is vacuous unless the ungated body actually moved",
	).is_greater(0.01)
	assert_float(held_travel).override_failure_message(
		"a held space must not advance a rolling body",
	).is_less(0.000001)


# The trap this suite fell into first, kept as a law because the failure is
# silent: the flag reads back exactly as it was set while the body keeps
# stepping, so a gate built on the wrong handle looks armed and does nothing.
func test_the_space_that_steps_a_body_is_the_one_the_body_names() -> void:
	var world := _world()
	await get_tree().physics_frame

	assert_bool(world.body.get_world_3d().space == world.space) \
			.override_failure_message(
				"the gate must hold the space the body resolves into, never the "
				+ "one its viewport declares",
			).is_true()
