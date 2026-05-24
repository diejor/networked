## Editor-side plugin that registers the "Networked" debugger tab.
##
## Handles the [EditorDebuggerPlugin] lifecycle: creates a [DebuggerSession] and
## [NetworkedDebuggerUI] per session. All incoming game messages are routed to
## [method DebuggerSession.receive]; the UI reacts to session signals instead
## of handling messages directly.
@tool
class_name NetworkedDebuggerPlugin
extends EditorDebuggerPlugin

# session_id -> DebuggerSession
var _sessions: Dictionary[int, DebuggerSession] = {}
# session_id -> NetworkedDebuggerUI (kept separately for breakpoint routing)
var _uis: Dictionary[int, NetworkedDebuggerUI] = {}
# peer_key -> session_id (tracks which session natively owns each peer)
var _local_peer_map: Dictionary[String, int] = {}
# peer_key -> stable_id ("source_path|tree_name|role|slot=N"). Independent
# of session_id and reporter_id so it survives game restarts and debugger
# session discard/recreate. The slot separates same-role windows.
var _peer_stable_id: Dictionary[String, String] = {}
# stable_id -> bool. Peers whose owning game window should be pinned on top.
# Persists for the entire editor lifetime; game close does not clear it.
var _pinned_peers: Dictionary[String, bool] = {}
# stable_id -> Rect2i. Last-known window geometry. Persists for editor
# lifetime.
var _geometry: Dictionary[String, Rect2i] = {}
# session_id -> Array[String]. Last known stable IDs owned by a debugger
# session. Kept after peer maps are cleared so late close-time geometry can
# still update the right persisted rects.
var _session_stable_ids: Dictionary[int, Array] = {}

var _dbg: NetwHandle = Netw.dbg.handle(self)


func _has_capture(prefix: String) -> bool:
	return prefix == "networked"


func _capture(message: String, data: Array, session_id: int) -> bool:
	if _capture_window_geometry(message, data, session_id):
		return true

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

	session.peer_registered.connect(
		_on_peer_registered.bind(session_id)
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

		_remember_peer_stable_ids(session_id, stale_peers)

		# Clean per-peer maps. _pinned_peers and _geometry are keyed by
		# stable_id / session_id and survive across restarts; the new peer
		# registrations below will rebind and the post-register handler will
		# call _recompute_pins if a stable_id is still pinned.
		for pk in stale_peers:
			_local_peer_map.erase(pk)
			_peer_stable_id.erase(pk)

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
			_remember_peer_stable_ids(session_id, owned_peers)
			
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

		_remember_peer_stable_ids(session_id, owned_peers)

		# Clean only the session-bound maps. _pinned_peers and _geometry are
		# keyed by stable_id (independent of session_id) and persist for the
		# whole editor lifetime so a closed-and-reopened game can re-bind.
		for pk in owned_peers:
			_local_peer_map.erase(pk)
			_peer_stable_id.erase(pk)

		# Notify OTHER sessions to remove these peers immediately.
		for other_id in _sessions:
			if other_id != session_id:
				_sessions[other_id].unregister_peers(owned_peers)

		# Recompute pins: surviving pinned windows are unaffected; this also
		# pushes a UI sync so the gone peer's checkbox state propagates.
		_recompute_pins.call_deferred()

	if session_id in _uis:
		_uis.erase(session_id)


func _on_peer_registered(
	peer_key: String,
	_display_name: String,
	tree_name: String,
	is_server: bool,
	_color: Color,
	is_remote: bool,
	_peer_id: int,
	session_id: int
) -> void:
	if is_remote:
		return
	var base_id := _base_stable_id_for(peer_key, tree_name, is_server)
	var stable_id: String = _peer_stable_id.get(peer_key, "")
	if stable_id.is_empty() or _stable_id_base(stable_id) != base_id:
		stable_id = _assign_stable_id(base_id)
	_local_peer_map[peer_key] = session_id
	_peer_stable_id[peer_key] = stable_id
	_remember_session_stable_id(session_id, stable_id)
	_migrate_unslotted_state(base_id, stable_id)
	if _pinned_peers.has(stable_id):
		_dbg.trace("Pin: re-applying for stable_id=%s on session=%d" \
			% [stable_id, session_id])
		_recompute_pins()


## Builds the base identity used for pin + geometry slot assignment.
## Independent of session_id and reporter_id.
func _base_stable_id_for(
	peer_key: String,
	tree_name: String,
	is_server: bool
) -> String:
	var source_path: String = peer_key.get_slice("|", 0)
	var role: String = "server" if is_server else "client"
	return "%s|%s|%s" % [source_path, tree_name, role]


func _assign_stable_id(base_id: String) -> String:
	var used_slots: Array[int] = []
	for pk: String in _peer_stable_id:
		if not _local_peer_map.has(pk):
			continue
		var stable_id: String = _peer_stable_id[pk]
		if _stable_id_base(stable_id) != base_id:
			continue
		used_slots.append(_stable_id_slot(stable_id))

	var slot := 0
	while slot in used_slots:
		slot += 1
	return "%s|slot=%d" % [base_id, slot]


func _stable_id_base(stable_id: String) -> String:
	var idx := stable_id.rfind("|slot=")
	if idx == -1:
		return stable_id
	return stable_id.substr(0, idx)


func _stable_id_slot(stable_id: String) -> int:
	var idx := stable_id.rfind("|slot=")
	if idx == -1:
		return 0
	return stable_id.substr(idx + 6).to_int()


func _migrate_unslotted_state(base_id: String, stable_id: String) -> void:
	if not _pinned_peers.has(base_id):
		return
	_pinned_peers[stable_id] = true
	_pinned_peers.erase(base_id)
	if _stable_id_slot(stable_id) != 0:
		return
	if _geometry.has(base_id) and not _geometry.has(stable_id):
		_geometry[stable_id] = _geometry[base_id]


## True if the peer's owning game window is currently pinned.
func is_peer_pinned(peer_key: String) -> bool:
	var sid: String = _peer_stable_id.get(peer_key, "")
	return not sid.is_empty() and _pinned_peers.has(sid)


## Toggles pin state for [param peer_key]. Called by any UI's peer-tree
## collapse/expand handler. [param source_session_id] identifies the UI that
## originated the toggle so it isn't synced back to itself.
func set_peer_pinned(
	peer_key: String,
	expanded: bool,
	source_session_id: int
) -> void:
	var sid: String = _peer_stable_id.get(peer_key, "")
	if sid.is_empty():
		_dbg.warn("Pin: ignored peer without stable_id %s", [peer_key])
		return
	if expanded:
		_pinned_peers[sid] = true
	else:
		_pinned_peers.erase(sid)
	_dbg.trace("Pin: requested stable_id=%s pinned=%s", [sid, expanded])
	_recompute_pins(source_session_id)


## Sends pin/unpin commands to game processes (with any stored geometry) and
## syncs the UI checkbox state across all UIs except [param source_session_id].
func _recompute_pins(source_session_id: int = -1) -> void:
	# For each pinned stable_id, find the owning session (if any) and what
	# geometry to apply. session_id -> Rect2i|null
	var pin_targets: Dictionary[int, Variant] = {}
	for sid_str: String in _pinned_peers:
		for pk: String in _peer_stable_id:
			if _peer_stable_id[pk] != sid_str:
				continue
			if not _local_peer_map.has(pk):
				continue
			var owner: int = _local_peer_map[pk]
			# Prefer stored geometry for this per-window stable_id, else null.
			pin_targets[owner] = _geometry.get(sid_str, null)

	for sid: int in pin_targets:
		_dbg.trace("Pin: sending pin_window to session=%d rect=%s", [
			sid,
			str(pin_targets[sid]),
		])
		send_to_game(sid, "networked:pin_window", [{"rect": pin_targets[sid]}])

	# Unpin any active session that isn't in the pinned set.
	for sid: int in _sessions:
		if pin_targets.has(sid):
			continue
		var s := get_session(sid)
		if s and s.is_active():
			_dbg.trace("Pin: sending unpin_window to session=%d", [sid])
			s.send_message("networked:unpin_window", [true])

	# Sync checkbox state across all other UIs. Iterate every known peer so
	# un-expand propagates as collapse.
	for ui_sid: int in _uis:
		if ui_sid == source_session_id:
			continue
		var ui: NetworkedDebuggerUI = _uis[ui_sid]
		if not is_instance_valid(ui):
			continue
		for pk: String in _local_peer_map:
			ui.sync_peer_expanded(pk, is_peer_pinned(pk))


func _on_window_geometry(session_id: int, data: Array) -> void:
	if data.is_empty() or not data[0] is Dictionary:
		return
	var d: Dictionary = data[0]
	var pos: Vector2i = d.get("position", Vector2i.ZERO)
	var size: Vector2i = d.get("size", Vector2i.ZERO)
	if size.x <= 0 or size.y <= 0:
		return
	var rect := Rect2i(pos, size)
	var stable_ids := _stable_ids_for_session(session_id)
	# Store under every stable_id hosted by this session; multiple peers in
	# one game process share the same window geometry.
	for sid_str: String in stable_ids:
		if sid_str.is_empty():
			continue
		_geometry[sid_str] = rect
	_dbg.trace("Pin: stored geometry %s for session=%d" % [rect, session_id])


func _capture_window_geometry(
	message: String,
	data: Array,
	session_id: int
) -> bool:
	if message == "window_geometry" or message == "networked:window_geometry":
		_on_window_geometry(session_id, data)
		return true

	var is_envelope := message == "envelope" or message == "networked:envelope"
	if not is_envelope or data.is_empty() or not data[0] is PackedByteArray:
		return false

	var envelope := NetEnvelope.from_dict(bytes_to_var(data[0]))
	if envelope.msg != &"networked:window_geometry":
		return false

	_on_window_geometry(session_id, [envelope.payload])
	return true


func _remember_peer_stable_ids(
	session_id: int,
	peer_keys: Array[String]
) -> void:
	for pk: String in peer_keys:
		var stable_id: String = _peer_stable_id.get(pk, "")
		if not stable_id.is_empty():
			_remember_session_stable_id(session_id, stable_id)


func _remember_session_stable_id(session_id: int, stable_id: String) -> void:
	if stable_id.is_empty():
		return
	if not _session_stable_ids.has(session_id):
		_session_stable_ids[session_id] = []
	var stable_ids: Array = _session_stable_ids[session_id]
	if stable_id not in stable_ids:
		stable_ids.append(stable_id)


func _stable_ids_for_session(session_id: int) -> Array[String]:
	var stable_ids: Array[String] = []
	for pk: String in _local_peer_map:
		if _local_peer_map[pk] != session_id:
			continue
		var stable_id: String = _peer_stable_id.get(pk, "")
		if stable_id.is_empty() or stable_id in stable_ids:
			continue
		stable_ids.append(stable_id)
		_remember_session_stable_id(session_id, stable_id)

	if not stable_ids.is_empty():
		return stable_ids

	var cached: Array = _session_stable_ids.get(session_id, [])
	for stable_id: String in cached:
		if stable_id not in stable_ids:
			stable_ids.append(stable_id)
	return stable_ids


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


## Returns [code]true[/code] when [param p_session_id] can receive messages.
func is_game_session_active(p_session_id: int) -> bool:
	var s := get_session(p_session_id)
	return s and s.is_active()
