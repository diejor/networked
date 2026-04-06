## Utility helpers for suppressing runtime node behaviour inside the editor.
class_name EditorTooling
extends RefCounted

## Disables all process callbacks on [param node] and optionally runs an editor validation.
##
## Returns [code]true[/code] when running inside the editor — callers should treat this as
## a signal to return early from [code]_ready()[/code].
## [codeblock]
## func _ready() -> void:
##     if EditorTooling.validate_and_halt(self, _validate_editor):
##         return
## [/codeblock]
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
