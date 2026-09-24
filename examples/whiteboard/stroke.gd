class_name Stroke
extends Line2D

var entity: NetwEntity


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.lifecycle = NetwEntity.LIFECYCLE_CONTROLLER
	Netw.configure_property(self, &"default_color").on_spawn()
	Netw.configure_property(self, &"points").broadcast()
