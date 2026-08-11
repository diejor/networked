class_name RacingVehicleSpawner
extends MultiplayerSpawner
## Spawns one racing [Vehicle] entity per accepted participant.
##
## Each car takes the next start slot in join order. Authored [Marker3D] children
## of a sibling [code]SpawnPoints[/code] node place the cars when present, so a
## track lays its own grid out; a computed grid is the fallback when none is
## authored.

const VEHICLE_SCENE := preload("res://examples/racing/scenes/vehicle.tscn")
const SPAWN_POINTS_PATH := ^"../SpawnPoints"
const GRID_COLUMNS := 4
const GRID_SPACING := 3.0
const START_ANCHOR := Vector3(5., 0.5, 5.0)


func _ready() -> void:
	spawn_function = _spawn_vehicle
	if multiplayer.is_server():
		# Deferred so the enclosing scene finishes binding its admission bus
		# before the first car goes live: an initial-scene spawn otherwise routes
		# the entity during the scene's own setup and misses player enrollment.
		_arm_spawns.call_deferred()


func _arm_spawns() -> void:
	var scene := NetwEntity.of(self).scene
	if not scene.is_declared:
		return
	scene.on_participant_entered(_on_participant_entered)
	for participant: NetwParticipant in scene.participants:
		_on_participant_entered(participant)


func _on_participant_entered(participant: NetwParticipant) -> void:
	spawn_participant(participant)


## Server only. Spawns one vehicle for [param participant].
func spawn_participant(participant: NetwParticipant) -> void:
	assert(multiplayer.is_server())
	if participant == null or participant.join == null:
		return
	if _has_vehicle(participant.join):
		return

	var ordered := NetwEntity.of(self).scene.participants
	ordered.sort_custom(
		func(a: NetwParticipant, b: NetwParticipant) -> bool:
			return a.peer_id < b.peer_id
	)
	var spawn_index := maxi(ordered.find(participant), 0)
	spawn({
		peer_id = participant.peer_id,
		spawn_index = spawn_index,
		username = _unique_username(str(participant.username)),
	})


# Duplicate display names would collide as entity ids, and the witness, the
# island roster, and every id-keyed diagnostic would conflate the cars. A
# later duplicate gains a join-order suffix instead.
func _unique_username(username: String) -> String:
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


func _spawn_vehicle(data: Dictionary) -> Node:
	var vehicle := VEHICLE_SCENE.instantiate()
	var peer_id := int(data.peer_id)
	var username := str(data.username)
	var spawn_index := int(data.spawn_index)
	NetwEntity.bind(vehicle, StringName(username), peer_id)

	# The root anchors at the origin and the sphere carries the offset, so the
	# on-spawn snapshot of sphere_position places every car on its start slot.
	var sphere := vehicle.get_node(^"Sphere") as Node3D
	if sphere:
		sphere.position = _spawn_slot(spawn_index)
	return vehicle


func _has_vehicle(rj: ResolvedJoin) -> bool:
	var root := get_node_or_null(spawn_path)
	return NetwEntity.find(root, rj) != null if root else false


# The world position of start slot [param spawn_index], from the authored spawn
# markers when a SpawnPoints node supplies them, wrapping when there are more cars
# than markers, else the computed grid.
func _spawn_slot(spawn_index: int) -> Vector3:
	var markers := _spawn_markers()
	if not markers.is_empty():
		return markers[spawn_index % markers.size()].global_position
	return _grid_slot(spawn_index)


func _spawn_markers() -> Array[Marker3D]:
	var out: Array[Marker3D] = []
	var container := get_node_or_null(SPAWN_POINTS_PATH)
	if container:
		for child in container.get_children():
			if child is Marker3D:
				out.append(child)
	return out


func _grid_slot(spawn_index: int) -> Vector3:
	var column := spawn_index % GRID_COLUMNS
	# The row index is the whole-columns count, so the truncation is the point.
	@warning_ignore("integer_division")
	var row := spawn_index / GRID_COLUMNS
	var offset := (GRID_COLUMNS - 1) * GRID_SPACING * 0.5
	return START_ANCHOR + Vector3(column * GRID_SPACING - offset, 0.0, row * GRID_SPACING)
