class_name RacingVehicleSpawner
extends MultiplayerSpawner

const VEHICLE_SCENE := preload("res://examples/racing/scenes/vehicle.tscn")
const SPAWN_POINTS_PATH := ^"../SpawnPoints"
const GRID_COLUMNS := 4
const GRID_SPACING := 3.0
const START_ANCHOR := Vector3(5., 0.5, 5.0)


func _ready() -> void:
	spawn_function = make_vehicle


func spawn_vehicle(participant: NetwParticipant, slot: int) -> void:
	assert(multiplayer.is_server())
	spawn(
		{
			peer_id = participant.peer_id,
			spawn_index = slot,
			username = unique_username(String(participant.username)),
		},
	)


func unique_username(username: String) -> String:
	var parent := get_node_or_null(spawn_path)
	var taken := { }
	if parent:
		for child in parent.get_children():
			var entity := NetwEntity.of(child)
			if entity:
				taken[String(entity.entity_id)] = true
	var claimed := username
	var ordinal := 2
	while taken.has(claimed):
		claimed = "%s-%d" % [username, ordinal]
		ordinal += 1
	return claimed


func make_vehicle(data: Dictionary) -> Node:
	var vehicle := VEHICLE_SCENE.instantiate()
	var peer_id := int(data.peer_id)
	var username := str(data.username)
	var spawn_index := int(data.spawn_index)
	NetwEntity.bind(vehicle, StringName(username), peer_id)

	var sphere := vehicle.get_node(^"Sphere") as Node3D
	if sphere:
		sphere.position = spawn_slot(spawn_index)
	return vehicle


func spawn_slot(spawn_index: int) -> Vector3:
	var markers := spawn_markers()
	if not markers.is_empty():
		return markers[spawn_index % markers.size()].global_position
	return grid_slot(spawn_index)


func spawn_markers() -> Array[Marker3D]:
	var out: Array[Marker3D] = []
	var container := get_node_or_null(SPAWN_POINTS_PATH)
	if container:
		for child in container.get_children():
			if child is Marker3D:
				out.append(child)
	return out


func grid_slot(spawn_index: int) -> Vector3:
	var column := spawn_index % GRID_COLUMNS
	@warning_ignore("integer_division")
	var row := spawn_index / GRID_COLUMNS
	var offset := (GRID_COLUMNS - 1) * GRID_SPACING * 0.5
	return START_ANCHOR + Vector3(column * GRID_SPACING - offset, 0.0, row * GRID_SPACING)
