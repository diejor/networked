## Conditionally removes debug-only nodes from non-debug builds.
extends Object

## True when debug-only in-world visuals should exist for this process.
static func is_world_debug_enabled() -> bool:
	if Engine.is_editor_hint() or not OS.is_debug_build():
		return false
	var scene_tree := Engine.get_main_loop() as SceneTree
	return scene_tree != null and scene_tree.debug_collisions_hint


## Frees [param node] unless the process is a debug build with collision hints
## enabled.
static func free_if_debug(node: Node) -> void:
	if Engine.is_editor_hint():
		return
	if not is_world_debug_enabled():
		node.queue_free()
