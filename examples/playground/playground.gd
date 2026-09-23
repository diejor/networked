extends Node3D

const AVATAR := preload("res://examples/playground/avatar.tscn")


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Playground")
	Netw.configure_spawn(spawn_avatar)


func spawn_avatar(slot: int) -> Node:
	var avatar := AVATAR.instantiate() as Node3D
	avatar.position = Vector3(slot * 2.0 - 2.0, 0.8, 6.0)
	return avatar
