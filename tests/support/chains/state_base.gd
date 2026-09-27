class_name StateSyncBody
extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").state()
