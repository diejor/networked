## Editor-side plugin that registers the "Networked" debugger tab.
##
## Handles the [EditorDebuggerPlugin] lifecycle: creates a [DebuggerSession] and
## [NetworkedDebuggerUI] per session. All incoming game messages are routed to
## [method DebuggerSession.receive]; the UI reacts to session signals rather than
## handling messages directly.
@tool
class_name NetworkedDebuggerPlugin
extends EditorDebuggerPlugin

# session_id → DebuggerSession
var _sessions: Dictionary[int, DebuggerSession] = {}
# session_id → NetworkedDebuggerUI  (kept separately for breakpoint routing)
var _uis: Dictionary[int, NetworkedDebuggerUI] = {}


func _has_capture(prefix: String) -> bool:
	return prefix == "networked"


func _capture(message: String, data: Array, session_id: int) -> bool:
	if session_id not in _sessions or not is_instance_valid(_sessions[session_id]):
		return true
	var session: DebuggerSession = _sessions[session_id]
	if message == "relay_forward" and not data.is_empty():
		var d: Dictionary = data[0]
		session.receive_remote(
			d.get("source_tree_name", ""),
			d.get("msg", ""),
			d.get("data", {})
		)
	else:
		session.receive(message, data)
	return true


func _setup_session(session_id: int) -> void:
	var session := DebuggerSession.new()
	session.plugin = self
	session.session_id = session_id
	_sessions[session_id] = session

	var ui := NetworkedDebuggerUI.new()
	ui.name = "Networked"
	ui.session = session
	_uis[session_id] = ui

	var godot_session := get_session(session_id)
	# Reset at the START of a new run so crash-time data survives for inspection.
	# Clearing on stopped wipes the ring buffers the instant the game crashes —
	# exactly when you need them most. _discard_session still clears on editor close.
	godot_session.started.connect(func() -> void:
		if is_instance_valid(session):
			session.reset()
	)
	godot_session.add_session_tab(ui)


func _discard_session(session_id: int) -> void:
	if session_id in _sessions:
		var session: DebuggerSession = _sessions[session_id]
		if is_instance_valid(session):
			session.reset()
		_sessions.erase(session_id)
	if session_id in _uis:
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
func send_to_game(p_session_id: int, message: String, data: Array) -> void:
	var s := get_session(p_session_id)
	if s and s.is_active():
		s.send_message(message, data)
