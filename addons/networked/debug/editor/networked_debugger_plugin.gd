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
	var session := get_session(session_id)
	# Reset at the START of a new run so crash-time data survives for inspection.
	# Clearing on stopped wipes the ring buffers the instant the game crashes — exactly
	# when you need them most. _discard_session still clears when the editor closes.
	session.started.connect(func() -> void:
		if is_instance_valid(ui):
			ui.reset_session()
	)
	session.add_session_tab(ui)
	_uis[session_id] = ui


func _discard_session(session_id: int) -> void:
	if session_id in _uis:
		var ui: NetworkedDebuggerUI = _uis[session_id]
		if is_instance_valid(ui):
			ui.reset_session()
		_uis.erase(session_id)


func _breakpoint_set_in_tree(script: Script, line: int, enabled: bool) -> void:
	for ui: NetworkedDebuggerUI in _uis.values():
		if is_instance_valid(ui):
			ui.on_breakpoint_changed(script.resource_path, line, enabled)


func _breakpoints_cleared_in_tree() -> void:
	for ui: NetworkedDebuggerUI in _uis.values():
		if is_instance_valid(ui):
			ui.on_breakpoints_cleared()


## Sends a message from the editor to the running game via the given session.
func send_to_game(session_id: int, message: String, data: Array) -> void:
	var s := get_session(session_id)
	if s and s.is_active():
		s.send_message(message, data)
