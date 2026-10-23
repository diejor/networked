extends Node2D

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"visible").state()
