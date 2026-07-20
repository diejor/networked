extends NetwTestSuite
## Isolates the physics solver: does Jolt reproduce itself, given identical
## inputs, in a single process?
##
## Every earlier measurement of "divergence" in this project compared two peers,
## so it mixed the solver together with clock phase, input delivery, the
## prediction lead, and the correction machinery. This removes all of that. Two
## worlds are built identically in one process, driven by the same scripted
## impulse sequence, and stepped by the same physics frames. Nothing is
## networked and nothing is corrected.
##
## [codeblock]
## world A  ─┐
##           ├─ same impulses, same frames, same binary  ─> compare state
## world B  ─┘
## [/codeblock]
##
## If the two states match, the solver reproduces and any divergence seen
## between peers comes from timing, delivery, or comparison, not from physics.
## If they separate, prediction of this body is not viable without replacing the
## integration.

# Mirrors the racing car's Sphere node so the answer applies to the real body,
# not to a generic test sphere.
const MASS := 1000.0
const RADIUS := 0.5
const GRAVITY_SCALE := 1.5
const LINEAR_DAMP := 0.1
const ANGULAR_DAMP := 4.0
const FRICTION := 5.0

# The car's only drive: an angular velocity push along the model's x axis.
const DRIVE_PER_TICK := 100.0

var _worlds: Array[SubViewport] = []


func after_test() -> void:
	for viewport in _worlds:
		if is_instance_valid(viewport):
			viewport.queue_free()
	_worlds.clear()


# Builds one isolated world holding a ground plane and a sphere matching the
# car's, and returns its body. Each world owns its own World3D, so the two share
# no physics space.
func _make_world() -> RigidBody3D:
	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	viewport.world_3d = World3D.new()
	add_child(viewport)
	auto_free(viewport)
	_worlds.append(viewport)

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
	viewport.add_child(ground)

	var sphere := RigidBody3D.new()
	var sphere_shape := CollisionShape3D.new()
	var ball := SphereShape3D.new()
	ball.radius = RADIUS
	sphere_shape.shape = ball
	sphere.add_child(sphere_shape)
	sphere.mass = MASS
	sphere.gravity_scale = GRAVITY_SCALE
	sphere.linear_damp = LINEAR_DAMP
	sphere.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.angular_damp = ANGULAR_DAMP
	sphere.continuous_cd = true
	var material := PhysicsMaterial.new()
	material.friction = FRICTION
	material.rough = true
	sphere.physics_material_override = material
	sphere.position = Vector3(0.0, RADIUS, 0.0)
	viewport.add_child(sphere)
	return sphere


# The scripted drive. Deterministic by construction: the push depends only on the
# tick index, never on the body's own state, so neither world can steer the other
# off through feedback.
func _drive(tick: int, delta: float) -> Vector3:
	var speed := minf(1.0, float(tick) * 0.02)
	var steer := sin(float(tick) * 0.05)
	return Vector3(1.0, 0.0, steer).normalized() * speed * DRIVE_PER_TICK * delta


func test_two_worlds_reproduce_under_identical_impulses() -> void:
	var a := _make_world()
	var b := _make_world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	var worst_position := 0.0
	var worst_angular := 0.0
	var checkpoints: Array[String] = []

	for tick in range(240):
		var push := _drive(tick, delta)
		a.angular_velocity += push
		b.angular_velocity += push
		await get_tree().physics_frame

		var position_gap := a.position.distance_to(b.position)
		var angular_gap := (a.angular_velocity - b.angular_velocity).length()
		worst_position = maxf(worst_position, position_gap)
		worst_angular = maxf(worst_angular, angular_gap)
		if (tick + 1) % 60 == 0:
			checkpoints.append(
				"t=%.1fs position=%.9f angular=%.9f" % [
					float(tick + 1) * delta, position_gap, angular_gap,
				],
			)

	for line in checkpoints:
		print("[jolt] %s" % line)
	print(
		"[jolt] worst over 240 ticks: position=%.9f angular=%.9f" % [
			worst_position, worst_angular,
		],
	)
	print("[jolt] engine: %s" % ProjectSettings.get_setting("physics/3d/physics_engine"))

	# Two worlds built the same way and driven the same way must land in the same
	# place. A solver that cannot reproduce itself in one process cannot be
	# predicted across two.
	assert_float(worst_position) \
		.override_failure_message(
			"same-process position divergence reached %.9f m" % worst_position,
		).is_less(0.001)
	assert_float(worst_angular) \
		.override_failure_message(
			"same-process angular divergence reached %.9f rad/s" % worst_angular,
		).is_less(0.001)


# The same question with contacts in play. Rolling contact is where a solver is
# most likely to order-swap, so the plain roll above could reproduce while a
# bouncing, re-contacting body does not.
func test_two_worlds_reproduce_across_repeated_contact() -> void:
	var a := _make_world()
	var b := _make_world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	# Drop both from the same height so each re-establishes ground contact
	# several times during the run.
	a.position = Vector3(0.0, 4.0, 0.0)
	b.position = Vector3(0.0, 4.0, 0.0)
	await get_tree().physics_frame

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	var worst_position := 0.0
	var worst_angular := 0.0
	for tick in range(240):
		var push := _drive(tick, delta)
		a.angular_velocity += push
		b.angular_velocity += push
		await get_tree().physics_frame
		worst_position = maxf(worst_position, a.position.distance_to(b.position))
		worst_angular = maxf(
			worst_angular, (a.angular_velocity - b.angular_velocity).length(),
		)

	print(
		"[jolt] with contact, worst over 240 ticks: position=%.9f angular=%.9f" % [
			worst_position, worst_angular,
		],
	)
	assert_float(worst_position) \
		.override_failure_message(
			"contact-path position divergence reached %.9f m" % worst_position,
		).is_less(0.001)
