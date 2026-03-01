class_name DebugLabel
extends Label

func _init() -> void:
	DebugFeature.free_if_debug(self)
