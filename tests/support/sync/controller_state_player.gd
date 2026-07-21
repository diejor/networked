## Minimal public state stream authored by the entity controller.
extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state().controller()
