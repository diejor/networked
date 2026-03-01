@tool
class_name DebugLabel
extends Label

func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	if not OS.is_debug_build():
		queue_free()
		return
	
	if not get_tree().debug_collisions_hint:
		queue_free()
		return
