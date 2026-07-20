extends Node3D

@onready var camera = $Camera

# The local player's spawned car, resolved lazily from the sibling Vehicles
# container (the fixed authored target is gone now that cars are spawned).
var target: Vehicle

# Functions

func _physics_process(delta):

	if not is_instance_valid(target):
		target = _find_local_vehicle()
	if not is_instance_valid(target):
		return

	# Ease position towards target vehicle position

	self.position = self.position.lerp(target.get_vehicle_position(), delta * 4)

	# Zoom camera based on the speed of the vehicle

	var speed_factor = clamp(abs(target.linear_speed), 0.0, 1.0)
	var target_z = remap(speed_factor, 0.0, 1.0, 10, 20)

	camera.position.z = lerp(camera.position.z, target_z, delta * 0.5)


func _find_local_vehicle() -> Vehicle:
	var vehicles := get_node_or_null(^"../Vehicles")
	if not vehicles:
		return null
	for vehicle: Node in vehicles.get_children():
		var e := NetwEntity.of(vehicle)
		if e and e.is_controlled_locally:
			return vehicle as Vehicle
	return null
