## Editor-side plugin that registers the "Networked" debugger tab.
##
## Handles the [EditorDebuggerPlugin] lifecycle: creates a [NetworkedDebuggerUI]
## per session and routes incoming game messages to it.
@tool
class_name NetworkedDebuggerPlugin
extends EditorDebuggerPlugin

# session_id → NetworkedDebuggerUI
var _uis: Dictionary[int, NetworkedDebuggerUI] = {}


func _has_capture(prefix: String) -> bool:
	return prefix == "networked"


func _capture(message: String, data: Array, session_id: int) -> bool:
	if session_id in _uis and is_instance_valid(_uis[session_id]):
		_uis[session_id].on_message(message, data)
	return true


func _setup_session(session_id: int) -> void:
	var ui := NetworkedDebuggerUI.new()
	ui.name = "Networked"
	ui.plugin = self
	ui.session_id = session_id
	get_session(session_id).add_session_tab(ui)
	_uis[session_id] = ui


## Sends a message from the editor to the running game via the given session.
func send_to_game(session_id: int, message: String, data: Array) -> void:
	var s := get_session(session_id)
	if s and s.is_active():
		s.send_message(message, data)
