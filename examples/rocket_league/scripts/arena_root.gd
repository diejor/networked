extends Node3D

func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Arena")
