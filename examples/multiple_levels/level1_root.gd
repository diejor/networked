extends Node2D

func _init() -> void:
	Netw.configure_scene(self).labeled(&"Level1").isolated()
