class_name RocketPlayerSpawner
extends MultiplayerSpawner

const CAR_SCENE := preload("res://examples/rocket_league/scenes/player_rb.tscn")

@onready var car_root: Node = get_node(spawn_path)


func _ready() -> void:
	spawn_function = spawn_car
	if multiplayer.is_server():
		arm_spawns.call_deferred()


func arm_spawns() -> void:
	var arena: NetwSceneHandle = NetwEntity.of(self).scene
	arena.observe(
		NetwMultiplayer.SCENE_EVENT_PARTICIPANT,
		participant_edge.bind(true),
	)
	for participant: NetwParticipant in arena.participants:
		add_car(participant)


func participant_edge(
		present: bool,
		participant: NetwParticipant,
		entered: bool,
) -> bool:
	if entered and present:
		add_car(participant)
	return false


func add_car(participant: NetwParticipant) -> void:
	if NetwEntity.find(car_root, participant) == null:
		spawn_car_for(String(participant.username), participant.peer_id)


func spawn_car_for(wanted_id: String, peer_id: int) -> void:
	var taken := cars()
	var team := taken.size() % 2
	var slot := 0
	for car: RocketCar in taken:
		if car.team == team:
			slot += 1
	spawn(
		{
			entity_id = unique_id(wanted_id),
			peer_id = peer_id,
			team = team,
			slot = slot,
		},
	)


func spawn_car(data: Dictionary) -> Node:
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
