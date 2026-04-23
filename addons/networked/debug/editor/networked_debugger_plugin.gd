## Editor-side plugin that registers the "Networked" debugger tab.
##
## Handles the [EditorDebuggerPlugin] lifecycle: creates a [DebuggerSession] and
## [NetworkedDebuggerUI] per session. All incoming game messages are routed to
## [method DebuggerSession.receive]; the UI reacts to session signals rather than
## handling messages directly.
@tool
class_name NetworkedDebuggerPlugin
extends EditorDebuggerPlugin

# session_id -> DebuggerSession
var _sessions: Dictionary[int, DebuggerSession] = {}
# session_id -> NetworkedDebuggerUI (kept separately for breakpoint routing)
var _uis: Dictionary[int, NetworkedDebuggerUI] = {}
# peer_key -> session_id (tracks which session natively owns each peer)
var _local_peer_map: Dictionary[String, int] = {}


func _has_capture(prefix: String) -> bool:
	return prefix == "networked"


func _capture(message: String, data: Array, session_id: int) -> bool:
	if session_id not in _sessions or not is_instance_valid(_sessions[session_id]):
		return true
	
	# Deliver locally (not remote).
	_sessions[session_id].receive(message, data, false)
	
	# Relay to all other active sessions (mark as remote).
	for other_id: int in _sessions:
		if other_id != session_id:
			_sessions[other_id].receive(message, data, true)
			
	return true


func _setup_session(session_id: int) -> void:
	var session := DebuggerSession.new()
	session.plugin = self
	session.session_id = session_id
	_sessions[session_id] = session

	# Map peer keys to this session as they register.
	session.peer_registered.connect(
		func(pk: String, _d: String, _t: String, _s: bool, _c: Color, rem: bool, _p: int):
			if not rem:
				_local_peer_map[pk] = session_id
	)

	var ui := NetworkedDebuggerUI.new()
	ui.name = "Networked"
	ui.session = session
	_uis[session_id] = ui

	var godot_session := get_session(session_id)
	# Reset at START so crash-time data survives for inspection.
	godot_session.started.connect(func() -> void:
		# Identify all peers previously owned by this session.
		var stale_peers: Array[String] = []
		for pk: String in _local_peer_map:
			if _local_peer_map[pk] == session_id:
				stale_peers.append(pk)
		
		# Notify OTHER sessions to remove these peers immediately.
		for other_id: int in _sessions:
			if other_id != session_id:
				_sessions[other_id].unregister_peers(stale_peers)
		
		# Clean up ownership map.
		for pk in stale_peers:
			_local_peer_map.erase(pk)

		if is_instance_valid(session):
			session.reset()
		# Request fresh snapshot so editor shows current state immediately.
		if godot_session.is_active():
			godot_session.send_message("networked:request_snapshot", [true])
	)
	godot_session.stopped.connect(func() -> void:
		if is_instance_valid(session):
			session.mark_all_offline()
			
			# Identify all peers owned by the stopped session.
			var owned_peers: Array[String] = []
			for pk: String in _local_peer_map:
				if _local_peer_map[pk] == session_id:
					owned_peers.append(pk)
			
			# Notify OTHER sessions that these peers are now offline.
			for other_id in _sessions:
				if other_id != session_id:
					_sessions[other_id].mark_peers_offline(owned_peers)
	)
	godot_session.add_session_tab(ui)


func _discard_session(session_id: int) -> void:
	if session_id in _sessions:
		var session: DebuggerSession = _sessions[session_id]
		if is_instance_valid(session):
			session.reset()
		_sessions.erase(session_id)
		
		# Identify all peers owned by the discarded session.
		var owned_peers: Array[String] = []
		for pk: String in _local_peer_map:
			if _local_peer_map[pk] == session_id:
				owned_peers.append(pk)
		
		# Clean up ownership map.
		for pk in owned_peers:
			_local_peer_map.erase(pk)
			
		# Notify OTHER sessions to remove these peers immediately.
		for other_id in _sessions:
			if other_id != session_id:
				_sessions[other_id].unregister_peers(owned_peers)
		
		# Notify survivors to re-tile as the instance pool has shrunk.
		_broadcast_tiling_update.call_deferred()
			
	if session_id in _uis:
		_uis.erase(session_id)


## Broadcasts a tiling update request to all active game sessions.
func _broadcast_tiling_update() -> void:
	Netw.dbg.trace("DebuggerPlugin: [Broadcast] Tiling update requested.")
	for session_id in _sessions:
		var s := get_session(session_id)
		if s and s.is_active():
			s.send_message("networked:tiling_update", [true])


## Copies history directly from the owner's session memory for a specific panel.
func sync_history(
	requester_id: int, 
	peer_key: String, 
	panel_name: String
) -> void:
	if not _local_peer_map.has(peer_key):
		return
		
	var owner_id: int = _local_peer_map[peer_key]
	var owner_s: DebuggerSession = _sessions.get(owner_id)
	var req_s: DebuggerSession = _sessions.get(requester_id)
	
	if not owner_s or not req_s:
		return
		
	var key: String = "%s:%s" % [peer_key, panel_name]
	var owner_adapter := owner_s.get_adapter(key)
	var req_adapter := req_s.get_adapter(key)
	
	if owner_adapter and req_adapter:
		# Direct memory copy of the processed ring buffer.
		req_adapter.populate(owner_adapter.ring_buffer)


## Sends a message to the game process that natively owns [param peer_key].
func route_to_owner(peer_key: String, message: String, data: Array) -> void:
	if not _local_peer_map.has(peer_key):
		return
		
	var owner_id: int = _local_peer_map[peer_key]
	var godot_session := get_session(owner_id)
	if godot_session and godot_session.is_active():
		godot_session.send_message(message, data)


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
