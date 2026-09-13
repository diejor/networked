extends Camera3D

const MIN_FOCUS_DISTANCE := 0.01

@export var target: Node3D
@export var height := 1.5
@export var target_distance := 5.0
@export var smooth_speed := 5.0

var current_position := Vector3.ZERO
var current_rotation := Basis.IDENTITY


func _process(delta: float) -> void:
	if not target:
		return

	var focus := target.global_position
	var desired_position := focus - target.global_transform.basis.z * target_distance
	desired_position.y = focus.y + height
	current_position = current_position.lerp(desired_position, smooth_speed * delta)
	global_position = current_position

	var toward := focus - global_position
	if toward.length_squared() < MIN_FOCUS_DISTANCE * MIN_FOCUS_DISTANCE:
		return

	var desired_rotation := Basis.looking_at(toward, Vector3.UP).orthonormalized()
	current_rotation = current_rotation.orthonormalized().slerp(
		desired_rotation,
		2.0 * smooth_speed * delta,
	)
	global_transform.basis = current_rotation
