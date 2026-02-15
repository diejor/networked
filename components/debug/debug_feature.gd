class_name DebugFeature
extends Node

func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	if not OS.is_debug_build():
		owner.queue_free()
		return
	
	if not get_tree().debug_collisions_hint:
		owner.queue_free()
		return
