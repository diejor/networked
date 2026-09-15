class_name RocketPlayerSpawner
extends MultiplayerSpawner

const CAR_SCENE := preload("res://examples/rocket_league/scenes/player_rb.tscn")

@onready var car_root: Node = get_node(spawn_path)


func _ready() -> void:
	spawn_function = make_car


func add_car(participant: NetwParticipant, grid_slot: int) -> void:
	assert(multiplayer.is_server())
	if NetwEntity.find(car_root, participant) != null:
		return
	@warning_ignore("integer_division")
	var slot := grid_slot / 2
	spawn(
		{
			entity_id = unique_id(String(participant.username)),
			peer_id = participant.peer_id,
			team = grid_slot % 2,
			slot = slot,
		},
	)


func make_car(data: Dictionary) -> Node:
	var car := CAR_SCENE.instantiate() as RocketCar
	car.team = int(data.team)
	car.slot = int(data.slot)
	NetwEntity.bind(car, StringName(data.entity_id), int(data.peer_id))
	return car


func cars() -> Array[RocketCar]:
	var out: Array[RocketCar] = []
	for child in car_root.get_children():
		if child is RocketCar:
			out.append(child)
	return out


func unique_id(wanted: String) -> String:
	var taken := { }
	for car: RocketCar in cars():
		taken[String(NetwEntity.of(car).entity_id)] = true
	var claimed := wanted
	var ordinal := 2
	while taken.has(claimed):
		claimed = "%s-%d" % [wanted, ordinal]
		ordinal += 1
	return claimed
