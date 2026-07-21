extends Node
## Writes exact float32 trajectories for cross-process Jolt comparison.
##
## Each process runs isolated rolling, drop, head-on dynamic contact, and
## off-center dynamic contact cases. Output filenames include the process id
## while file contents do not, so runs can be byte-compared directly.

const MASS := 1000.0
const RADIUS := 0.5
const GRAVITY_SCALE := 1.5
const LINEAR_DAMP := 0.1
const ANGULAR_DAMP := 4.0
const FRICTION := 5.0
const DRIVE_PER_TICK := 100.0
const COLLISION_SPEED := 8.0
const RUN_TICKS := 240


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


var _viewports: Array[SubViewport] = []


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var single_bodies := {
		&"rolling": _make_world(RADIUS),
		&"drop": _make_world(4.0),
	}
	var collision_pairs := {
		&"dynamic_head_on": _make_collision_world(0.0),
		&"dynamic_off_center": _make_collision_world(0.35),
	}
	await get_tree().physics_frame
	await get_tree().physics_frame
	_start_collision(
		collision_pairs[&"dynamic_head_on"],
		Vector3(0.0, 0.0, -COLLISION_SPEED / RADIUS),
		Vector3(0.0, 0.0, COLLISION_SPEED / RADIUS),
	)
	_start_collision(
		collision_pairs[&"dynamic_off_center"],
		Vector3(4.0, 3.0, -16.0),
		Vector3(-3.0, -2.0, 16.0),
	)

	var single_files: Dictionary = { }
	var collision_files: Dictionary = { }
	var process_id := OS.get_process_id()
	for case_name: StringName in single_bodies:
		var file := _open_case_file(case_name, process_id)
		if not file:
			get_tree().quit(ERR_CANT_OPEN)
			return
		file.store_line("tick,position_bits,angular_velocity_bits")
		single_files[case_name] = file
	for case_name: StringName in collision_pairs:
		var file := _open_case_file(case_name, process_id)
		if not file:
			get_tree().quit(ERR_CANT_OPEN)
			return
		file.store_line(
			"tick,first_position_bits,first_orientation_bits,"
			+ "first_linear_velocity_bits,first_angular_velocity_bits,"
			+ "first_sleeping,second_position_bits,second_orientation_bits,"
			+ "second_linear_velocity_bits,second_angular_velocity_bits,"
			+ "second_sleeping",
		)
		collision_files[case_name] = file

	var delta := 1.0 / float(Engine.physics_ticks_per_second)
	for tick in range(RUN_TICKS):
		var push := _drive(tick, delta)
		for body: RigidBody3D in single_bodies.values():
			body.angular_velocity += push
		await get_tree().physics_frame
		for case_name: StringName in single_bodies:
			_write_state(single_files[case_name], tick, single_bodies[case_name])
		for case_name: StringName in collision_pairs:
			_write_collision_state(
				collision_files[case_name],
				tick,
				collision_pairs[case_name],
			)

	for file: FileAccess in single_files.values():
		file.close()
	for file: FileAccess in collision_files.values():
		file.close()
	for case_name: StringName in collision_pairs:
		var pair: _CollisionPair = collision_pairs[case_name]
		if pair.contact_count == 0:
			push_error("%s did not produce dynamic contact" % case_name)
			get_tree().quit(ERR_BUG)
			return
	print(
		"[jolt-bitdiff] complete pid=%d engine=%s ticks=%d"
		% [
			process_id,
			ProjectSettings.get_setting("physics/3d/physics_engine"),
			RUN_TICKS,
		],
	)
	get_tree().quit(OK)


# Opens one process-unique trace whose contents remain directly comparable.
func _open_case_file(case_name: StringName, process_id: int) -> FileAccess:
	var path := "user://jolt_probe_%s_%d.csv" % [case_name, process_id]
	var file := FileAccess.open(path, FileAccess.WRITE)
	if not file:
		push_error("jolt bit-diff probe could not open %s" % path)
		return null
	print("[jolt-bitdiff] %s" % ProjectSettings.globalize_path(path))
	return file


# Builds one isolated racing-sphere world at the requested start height.
func _make_world(start_height: float) -> RigidBody3D:
	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	viewport.world_3d = World3D.new()
	add_child(viewport)
	_viewports.append(viewport)
	_add_ground(viewport)
	return _add_sphere(viewport, Vector3(0.0, start_height, 0.0))


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


# Builds two equal dynamic racing spheres in one isolated world.
func _make_collision_world(lateral_offset: float) -> _CollisionPair:
	var first := _make_world(RADIUS)
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


# Starts one equal-mass collision after the worlds have settled.
func _start_collision(
		pair: _CollisionPair,
		first_spin: Vector3,
		second_spin: Vector3,
) -> void:
	pair.first.linear_velocity = Vector3(COLLISION_SPEED, 0.0, 0.0)
	pair.second.linear_velocity = Vector3(-COLLISION_SPEED, 0.0, 0.0)
	pair.first.angular_velocity = first_spin
	pair.second.angular_velocity = second_spin


# Returns a state-independent impulse matching the determinism test rig.
func _drive(tick: int, delta: float) -> Vector3:
	var speed := minf(1.0, float(tick) * 0.02)
	var steer := sin(float(tick) * 0.05)
	return (
			Vector3(1.0, 0.0, steer).normalized()
			* speed
			* DRIVE_PER_TICK
			* delta
	)


func _write_state(file: FileAccess, tick: int, body: RigidBody3D) -> void:
	file.store_line(
		"%d,%s,%s"
		% [
			tick,
			_vector_bits(body.position),
			_vector_bits(body.angular_velocity),
		],
	)


func _write_collision_state(
		file: FileAccess,
		tick: int,
		pair: _CollisionPair,
) -> void:
	file.store_line(
		"%d,%s,%s,%s,%s,%d,%s,%s,%s,%s,%d"
		% [
			tick,
			_vector_bits(pair.first.position),
			_quaternion_bits(pair.first.quaternion),
			_vector_bits(pair.first.linear_velocity),
			_vector_bits(pair.first.angular_velocity),
			int(pair.first.sleeping),
			_vector_bits(pair.second.position),
			_quaternion_bits(pair.second.quaternion),
			_vector_bits(pair.second.linear_velocity),
			_vector_bits(pair.second.angular_velocity),
			int(pair.second.sleeping),
		],
	)


func _vector_bits(value: Vector3) -> String:
	return (
			PackedFloat32Array([value.x, value.y, value.z])
			.to_byte_array()
			.hex_encode()
	)


func _quaternion_bits(value: Quaternion) -> String:
	return (
			PackedFloat32Array([value.x, value.y, value.z, value.w])
			.to_byte_array()
			.hex_encode()
	)
