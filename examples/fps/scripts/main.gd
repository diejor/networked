extends Node3D

const PLAYER := preload("res://examples/fps/objects/player.tscn")


func _init() -> void:
	Netw.configure_scene(self).labeled(&"Main")
	Netw.configure_spawn(spawn_player)


func spawn_player(slot: int) -> Node:
	var player := PLAYER.instantiate()
	player.position = Vector3(slot % 4 - 1.5, 0.5, 0)
	player.skin = slot % player.skins.size()
	return player
