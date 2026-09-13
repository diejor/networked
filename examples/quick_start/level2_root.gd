extends Node2D

func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Level2").isolated()
