## Simple debug label that automatically frees itself in non-debug builds.
class_name DebugLabel
extends Label

const DebugFeature := preload("res://addons/networked/debug/ui/debug_feature.gd")


func _init() -> void:
	DebugFeature.free_if_debug(self)
