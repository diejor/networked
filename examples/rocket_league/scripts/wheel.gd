class_name RocketWheel
extends RayCast3D

@export var is_front_wheel: bool = true

var previous_spring_length: float = 0.0

@onready var car: RocketCar = get_parent().get_parent()
@onready var wheel: Node3D = $Wheel


func _ready() -> void:
	add_exception(car)


func wheel_tick(delta: float) -> void:
	force_raycast_update()
	if not is_colliding():
		set_wheel_position(-car.suspension_rest_dist)
		return

	var contact := get_collision_point()
	suspension(delta, contact)
	acceleration(contact)
	apply_z_force(contact)
	apply_x_force(delta, contact)
	set_wheel_position(to_local(contact).y + car.wheel_radius)
	rotate_wheel(delta)


func tire_velocity() -> Vector3:
	return car.state.get_velocity_at_local_position(
		global_position - car.global_position,
	)


func apply_x_force(delta: float, contact: Vector3) -> void:
	var dir: Vector3 = global_basis.x
	var lateral := dir.dot(tire_velocity())
	var grip: float = car.front_tire_grip if is_front_wheel \
	else car.rear_tire_grip
	car.apply_force(
		dir * (-lateral * grip / delta),
		contact - car.global_position,
	)


func apply_z_force(contact: Vector3) -> void:
	var dir: Vector3 = global_basis.z
	var force := dir.dot(tire_velocity()) * car.mass / 10.0
	car.apply_force(-dir * force, contact - car.global_position)


func acceleration(contact: Vector3) -> void:
	if is_front_wheel:
		return
	car.apply_force(
		-global_basis.z * (car.accel_input * car.engine_power),
		contact - car.global_position,
	)


func suspension(delta: float, contact: Vector3) -> void:
	var distance := contact.distance_to(global_position)
	var spring_length := clampf(
		distance - car.wheel_radius,
		0.0,
		car.suspension_rest_dist,
	)
	var spring: float = car.spring_strength \
			* (car.suspension_rest_dist - spring_length)
	var damper: float = car.spring_damper \
			* ((previous_spring_length - spring_length) / delta)
	previous_spring_length = spring_length
	var point := contact + Vector3(0.0, car.wheel_radius, 0.0)
	var force := basis.y * (spring + damper)
	car.apply_force(global_basis.y * force, point - car.global_position)


func set_wheel_position(new_y: float) -> void:
	wheel.position.y = lerpf(wheel.position.y, new_y, 0.6)


func rotate_wheel(delta: float) -> void:
	var forward: Vector3 = car.basis.z
	var sign_of := 1.0 if car.linear_velocity.dot(forward) > 0.0 else -1.0
	wheel.rotate_x(sign_of * car.linear_velocity.length() * delta)
