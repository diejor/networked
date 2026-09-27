extends Node3D

const CAR_SCENE := preload("res://examples/rocket_league/scenes/player_rb.tscn")


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Arena")
	Netw.configure_spawn(spawn_car)


func spawn_car(team: int, slot: int) -> Node:
	var car := CAR_SCENE.instantiate() as RocketCar
	car.team = team
	car.slot = slot
	return car


func cars() -> Array[RocketCar]:
	var out: Array[RocketCar] = []
	for child in get_node(^"Players").get_children():
		if child is RocketCar:
			out.append(child)
	return out
