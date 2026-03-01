class_name DebugFeature
extends Object

static func free_if_debug(node: Node) -> void:
	if Engine.is_editor_hint():
		return
	
	if not OS.is_debug_build():
		node.queue_free()
		return
	
	var scene_tree := Engine.get_main_loop() as SceneTree
	if not scene_tree.debug_collisions_hint:
		node.queue_free()
		return
