## Utility class for conditionally removing debug-only nodes from non-debug builds.
class_name DebugFeature
extends Object

## Frees [param node] unless the process is both a debug build and has collision hints enabled.
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
