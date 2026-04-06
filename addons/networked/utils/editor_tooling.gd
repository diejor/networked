class_name EditorTooling
extends RefCounted

## Runs editor validation and completely disables the node's runtime callbacks in the editor.
## Returns `true` if the engine is in the editor, signaling the caller to abort `_ready()`.

static func validate_and_halt(node: Node, validation_func: Callable = Callable()) -> bool:
	if not Engine.is_editor_hint():
		return false
		
	if validation_func.is_valid():
		validation_func.call()
		
	node.set_process(false)
	node.set_physics_process(false)
	node.set_process_input(false)
	node.set_process_unhandled_input(false)
	node.set_process_unhandled_key_input(false)
	
	return true
