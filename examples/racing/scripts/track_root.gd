extends Node3D

const VEHICLE_SCENE := preload("res://examples/racing/scenes/vehicle.tscn")
const GRID_COLUMNS := 4
const GRID_SPACING := 3.0
const START_ANCHOR := Vector3(5., 0.5, 5.0)


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Track")
	Netw.configure_spawn(spawn_vehicle)


func spawn_vehicle(slot: int) -> Node:
	var vehicle := VEHICLE_SCENE.instantiate()
	var sphere := vehicle.get_node(^"Sphere") as Node3D
	sphere.position = spawn_slot(slot)
	return vehicle


func spawn_slot(slot: int) -> Vector3:
	var markers := spawn_markers()
	if markers.is_empty():
		return grid_slot(slot)
	return markers[slot % markers.size()].global_position


func spawn_markers() -> Array[Marker3D]:
	var out: Array[Marker3D] = []
	for child in get_node(^"SpawnPoints").get_children():
		if child is Marker3D:
			out.append(child)
	return out


func grid_slot(slot: int) -> Vector3:
	var column := slot % GRID_COLUMNS
	@warning_ignore("integer_division")
	var row := slot / GRID_COLUMNS
	var offset := (GRID_COLUMNS - 1) * GRID_SPACING * 0.5
	return START_ANCHOR + Vector3(
		column * GRID_SPACING - offset,
		0.0,
		row * GRID_SPACING,
	)
