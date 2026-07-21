extends NetwTestSuite
## Isolates the physics solver by comparing identical independent worlds.
##
## Every earlier measurement of divergence in this project compared two peers,
## so it mixed the solver together with clock phase, input delivery, prediction
## lead, and correction machinery. This suite removes those variables. Two
## worlds are built identically in one process and stepped on the same physics
## frames. Nothing is networked and nothing is corrected.
##
## [codeblock]
## world A  --+
##            +-- same state, commands, and frames -> compare state
## world B  --+
## [/codeblock]
##
## Dynamic collision cases compare pose, both velocities, and sleep state for
## every corresponding body. They also prove that the intended contact occurred.

# Mirrors the racing car's Sphere node so the answer applies to the real body.
const MASS := 1000.0
const RADIUS := 0.5
const GRAVITY_SCALE := 1.5
const LINEAR_DAMP := 0.1
const ANGULAR_DAMP := 4.0
const FRICTION := 5.0

# The car's only drive is an angular velocity push along the model's x axis.
const DRIVE_PER_TICK := 100.0
const COLLISION_SPEED := 8.0
const COLLISION_TICKS := 180


class _CollisionPair:
	var first: RigidBody3D
	var second: RigidBody3D
	var contact_count: int = 0


	func _init(first_body: RigidBody3D, second_body: RigidBody3D) -> void:
		first = first_body
		second = second_body


	func _on_first_body_entered(body: Node) -> void:
		if body == second:
			contact_count += 1


class _CollisionResult:
	var first_world_contacts: int = 0
	var second_world_contacts: int = 0
	var worst_position: float = 0.0
	var worst_orientation: float = 0.0
	var worst_linear: float = 0.0
	var worst_angular: float = 0.0
	var sleeping_mismatch: bool = false


var _worlds: Array[SubViewport] = []


## Releases each isolated physics world after its test.
func after_test() -> void:
	for viewport in _worlds:
		if is_instance_valid(viewport):
			viewport.queue_free()
	_worlds.clear()


# Builds one isolated world with a ground plane and one racing sphere.
func _make_world() -> RigidBody3D:
	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	viewport.world_3d = World3D.new()
	add_child(viewport)
	auto_free(viewport)
	_worlds.append(viewport)
	_add_ground(viewport)
	return _add_sphere(viewport, Vector3(0.0, RADIUS, 0.0))


# Adds the racing ground configuration to one isolated world.
func _add_ground(viewport: SubViewport) -> void:
	var ground := StaticBody3D.new()
	var ground_shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(200.0, 1.0, 200.0)
	ground_shape.shape = box
	ground.add_child(ground_shape)
	ground.position = Vector3(0.0, -0.5, 0.0)
	var material := PhysicsMaterial.new()
	material.friction = FRICTION
	material.rough = true
	ground.physics_material_override = material
	viewport.add_child(ground)


# Adds one body with the racing sphere's physics configuration.
func _add_sphere(viewport: SubViewport, start_position: Vector3) -> RigidBody3D:
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
	sphere.position = start_position
	viewport.add_child(sphere)
	return sphere


# Builds two equal dynamic bodies in one isolated world.
func _make_collision_world(lateral_offset: float) -> _CollisionPair:
	var first := _make_world()
	var viewport := first.get_parent() as SubViewport
	first.position = Vector3(-1.5, RADIUS, -lateral_offset)
	var second := _add_sphere(
		viewport,
		Vector3(1.5, RADIUS, lateral_offset),
	)
	first.contact_monitor = true
	first.max_contacts_reported = 4
	var pair := _CollisionPair.new(first, second)
	first.body_entered.connect(pair._on_first_body_entered)
	return pair


# Returns a state-independent drive matching the racing determinism probe.
func _drive(tick: int, delta: float) -> Vector3:
	var speed := minf(1.0, float(tick) * 0.02)
	var steer := sin(float(tick) * 0.05)
	return (
			Vector3(1.0, 0.0, steer).normalized()
			* speed
			* DRIVE_PER_TICK
			* delta
	)


# Records the largest complete-body difference between matching worlds.
func _record_body_gap(
		result: _CollisionResult,
		first: RigidBody3D,
		second: RigidBody3D,
) -> void:
	result.worst_position = maxf(
		result.worst_position,
		first.position.distance_to(second.position),
	)
	result.worst_orientation = maxf(
		result.worst_orientation,
		_quaternion_gap(first.quaternion, second.quaternion),
	)
	result.worst_linear = maxf(
		result.worst_linear,
		(first.linear_velocity - second.linear_velocity).length(),
	)
	result.worst_angular = maxf(
		result.worst_angular,
		(first.angular_velocity - second.angular_velocity).length(),
	)
	result.sleeping_mismatch = (
			result.sleeping_mismatch or first.sleeping != second.sleeping
	)


# Returns component distance without angle conversion's acos precision floor.
func _quaternion_gap(first: Quaternion, second: Quaternion) -> float:
	return sqrt(
		pow(first.x - second.x, 2.0)
		+ pow(first.y - second.y, 2.0)
		+ pow(first.z - second.z, 2.0)
		+ pow(first.w - second.w, 2.0),
	)


# Runs equal dynamic collisions in two independent physics worlds.
func _run_dynamic_collision(
		lateral_offset: float,
		first_spin: Vector3,
		second_spin: Vector3,
) -> _CollisionResult:
	var first_world := _make_collision_world(lateral_offset)
	var second_world := _make_collision_world(lateral_offset)
	await get_tree().physics_frame
	await get_tree().physics_frame

	var pairs: Array[_CollisionPair] = [first_world, second_world]
	for pair in pairs:
		pair.first.linear_velocity = Vector3(COLLISION_SPEED, 0.0, 0.0)
		pair.second.linear_velocity = Vector3(-COLLISION_SPEED, 0.0, 0.0)
		pair.first.angular_velocity = first_spin
		pair.second.angular_velocity = second_spin

	var result := _CollisionResult.new()
	for _tick in range(COLLISION_TICKS):
		await get_tree().physics_frame
		_record_body_gap(result, first_world.first, second_world.first)
		_record_body_gap(result, first_world.second, second_world.second)

	result.first_world_contacts = first_world.contact_count
	result.second_world_contacts = second_world.contact_count
	return result


# Asserts that contact happened and corresponding bodies remained equivalent.
func _assert_collision_result(
		result: _CollisionResult,
		scenario: String,
) -> void:
	print(
		(
				"[jolt] %s dynamic contact: position=%.9f orientation=%.9f "
				+ "linear=%.9f angular=%.9f contacts=%d/%d"
		)
		% [
			scenario,
			result.worst_position,
			result.worst_orientation,
			result.worst_linear,
			result.worst_angular,
			result.first_world_contacts,
			result.second_world_contacts,
		],
	)
	assert_int(result.first_world_contacts) \
			.override_failure_message("%s collision did not occur" % scenario) \
			.is_greater(0)
	assert_int(result.second_world_contacts).is_equal(
		result.first_world_contacts,
	)
	assert_float(result.worst_position).is_less(0.001)
	assert_float(result.worst_orientation).is_less(0.001)
	assert_float(result.worst_linear).is_less(0.001)
	assert_float(result.worst_angular).is_less(0.001)
	assert_bool(result.sleeping_mismatch).is_false()


## Verifies identical scripted impulses in isolated worlds.
func test_two_worlds_reproduce_under_identical_impulses() -> void:
	var first := _make_world()
	var second := _make_world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	var worst_position := 0.0
	var worst_angular := 0.0
	var checkpoints: Array[String] = []

	for tick in range(240):
		var push := _drive(tick, delta)
		first.angular_velocity += push
		second.angular_velocity += push
		await get_tree().physics_frame

		var position_gap := first.position.distance_to(second.position)
		var angular_gap := (
				first.angular_velocity - second.angular_velocity
		).length()
		worst_position = maxf(worst_position, position_gap)
		worst_angular = maxf(worst_angular, angular_gap)
		if (tick + 1) % 60 == 0:
			checkpoints.append(
				"t=%.1fs position=%.9f angular=%.9f"
				% [float(tick + 1) * delta, position_gap, angular_gap],
			)

	for line in checkpoints:
		print("[jolt] %s" % line)
	print(
		"[jolt] worst over 240 ticks: position=%.9f angular=%.9f"
		% [worst_position, worst_angular],
	)
	print(
		"[jolt] engine: %s"
		% ProjectSettings.get_setting("physics/3d/physics_engine"),
	)
	assert_float(worst_position).is_less(0.001)
	assert_float(worst_angular).is_less(0.001)


## Verifies repeated dynamic-to-static ground contact in isolated worlds.
func test_two_worlds_reproduce_across_repeated_contact() -> void:
	var first := _make_world()
	var second := _make_world()
	await get_tree().physics_frame
	await get_tree().physics_frame

	first.position = Vector3(0.0, 4.0, 0.0)
	second.position = Vector3(0.0, 4.0, 0.0)
	await get_tree().physics_frame

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	var worst_position := 0.0
	var worst_angular := 0.0
	for tick in range(240):
		var push := _drive(tick, delta)
		first.angular_velocity += push
		second.angular_velocity += push
		await get_tree().physics_frame
		worst_position = maxf(
			worst_position,
			first.position.distance_to(second.position),
		)
		worst_angular = maxf(
			worst_angular,
			(first.angular_velocity - second.angular_velocity).length(),
		)

	print(
		"[jolt] with contact: position=%.9f angular=%.9f"
		% [worst_position, worst_angular],
	)
	assert_float(worst_position).is_less(0.001)
	assert_float(worst_angular).is_less(0.001)


## Verifies equal-mass head-on dynamic contact in isolated worlds.
func test_two_worlds_reproduce_dynamic_head_on_collision() -> void:
	var rolling_spin := COLLISION_SPEED / RADIUS
	var result: _CollisionResult = await _run_dynamic_collision(
		0.0,
		Vector3(0.0, 0.0, -rolling_spin),
		Vector3(0.0, 0.0, rolling_spin),
	)
	_assert_collision_result(result, "head-on")


## Verifies off-center dynamic contact with asymmetric angular velocities.
func test_two_worlds_reproduce_dynamic_off_center_collision() -> void:
	var result: _CollisionResult = await _run_dynamic_collision(
		0.35,
		Vector3(4.0, 3.0, -16.0),
		Vector3(-3.0, -2.0, 16.0),
	)
	_assert_collision_result(result, "off-center")
