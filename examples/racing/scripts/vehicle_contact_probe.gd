class_name VehicleContactProbe
extends RigidBody3D
## Captures read-only solver contact facts for an armed racing netlog or a
## scripted regime capture.
##
## The script is attached at runtime, by [Vehicle] when
## [code]NETW_NETLOG[/code] is set and by [RacingRegime] for every scripted
## run. It reads [PhysicsDirectBodyState3D] without changing forces or body
## state. The per-frame counts reset each integration; the cumulative
## [code]*_frames[/code] counters do not, so a whole run's contact prevalence
## is read as [code]wall_contact_frames / contact_sequence[/code].

const CAR_LAYER := 8
const WALL_NORMAL_Y_MAX := 0.5

var contact_sequence: int = 0
var wall_contacts: int = 0
var car_contacts: int = 0
var ground_contacts: int = 0
var wall_impulse: float = 0.0
var wall_normal_y: float = 0.0
var wall_contact_frames: int = 0
var car_contact_frames: int = 0


# Samples every reported contact without altering the built-in integration.
func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
	contact_sequence += 1
	wall_contacts = 0
	car_contacts = 0
	ground_contacts = 0
	wall_impulse = 0.0
	wall_normal_y = 0.0
	for contact_index in range(state.get_contact_count()):
		var normal := state.get_contact_local_normal(contact_index).normalized()
		var impulse := state.get_contact_impulse(contact_index).length()
		var collider := state.get_contact_collider_object(contact_index)
		if (_collision_layer(collider) & CAR_LAYER) != 0:
			car_contacts += 1
		elif absf(normal.y) <= WALL_NORMAL_Y_MAX:
			wall_contacts += 1
			if impulse >= wall_impulse:
				wall_impulse = impulse
				wall_normal_y = normal.y
		else:
			ground_contacts += 1
	if wall_contacts > 0:
		wall_contact_frames += 1
	if car_contacts > 0:
		car_contact_frames += 1


# Reads a collider layer across CollisionObject3D and GridMap bodies.
func _collision_layer(collider: Object) -> int:
	if not is_instance_valid(collider):
		return 0
	if collider.has_method(&"get_collision_layer"):
		return int(collider.call(&"get_collision_layer"))
	return 0
