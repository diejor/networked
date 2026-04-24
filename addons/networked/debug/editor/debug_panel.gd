## Base class for all Networked Debugger panel controls.
##
## Defines the interface contract that [PanelWrapper] relies on for peer-context
## hooks and data lifecycle. Panels that do not extend this are treated as 
## opaque [Control] nodes by the wrapper — they simply don't participate 
## in the hook protocol.
@tool
class_name DebugPanel
extends VBoxContainer


func _init() -> void:
	add_theme_constant_override("separation", 4)


## Called by [PanelWrapper.set_online] when the owning peer goes offline or back online.
## Override to disable interactive controls that send RPCs to a dead tree.
func set_peer_online(_online: bool) -> void:
	pass


## Called by [PanelWrapper.init_peer_context] once, right after the panel enters
## the scene tree, when the peer is known to be relay-forwarded from another
## debugger session.
## Override to adjust live-only behavior.
func set_peer_remote(_remote: bool) -> void:
	pass


## Fills the panel from the full adapter ring buffer.
## Called when the panel checkbox is first checked (after the panel is in the tree).
func populate(_buffer: Array) -> void:
	pass


## Pushes a single new entry, called per [signal PanelDataAdapter.data_changed].
func on_new_entry(_entry: Variant) -> void:
	pass


## Clears all displayed data.
func clear() -> void:
	pass
