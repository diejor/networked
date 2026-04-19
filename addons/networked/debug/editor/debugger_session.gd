## Owns the peer registry and adapter lifecycle for one debug session.
##
## [NetworkedDebuggerPlugin] calls [method receive] for every incoming game message.
## This class routes each message to the appropriate [PanelDataAdapter] subclass and
## emits signals that [NetworkedDebuggerUI] reacts to. The UI never touches ring
## buffers directly — it reads them once on panel activation and then receives
## [signal adapter_data_changed] notifications for subsequent entries.
##
## Peer identity is keyed by [code]tree_name[/code] (the MultiplayerTree node name)
## because [code]networked:session_registered[/code] does not include a peer ID.
## The peer ID is populated lazily from the first [code]peer_connected[/code] event.
@tool
class_name DebuggerSession
extends RefCounted

## Emitted when a new MultiplayerTree peer completes registration.
## [param color] is the pre-assigned peer color; it never changes after this.
## [param is_remote] is true for peers that arrived via the relay bridge rather
## than a direct [EditorDebuggerSession] connection.
signal peer_registered(tree_name: String, is_server: bool, color: Color, is_remote: bool)

## Emitted when a peer's online status changes.
signal peer_status_changed(tree_name: String, online: bool)

## Emitted when a previously [remote] peer gains a direct editor connection and
## is promoted to [local]. The tree_name is unchanged.
signal peer_promoted(tree_name: String)


## Emitted after an adapter appends a new entry to its ring buffer.
signal adapter_data_changed(adapter_key: String)

## Emitted after [method reset] wipes all state.
signal session_cleared()

## Injected by [NetworkedDebuggerPlugin] at construction time.
var plugin: NetworkedDebuggerPlugin
var session_id: int

## Peer registry: tree_name → {is_server, backend_class, online, color, peer_id, is_remote}
## peer_id is 0 until the first peer_connected/disconnected message for this tree.
## is_remote is true for peers that arrived via the relay and have no direct editor session.
var _peers: Dictionary[String, Dictionary] = {}

## Relay messages that arrived before their session_registered was received.
## Drained the moment session_registered comes in for that tree_name.
var _pending_remote: Dictionary = {}  # tree_name → Array[{msg, data}]

## All adapters: adapter_key → PanelDataAdapter subclass instance.
var _adapters: Dictionary[String, PanelDataAdapter] = {}

## Alias map shared by all [CrashAdapter] instances (NodePath prefix → readable alias).
## Populated by networked:lobby_event messages; passed by reference to each CrashAdapter.
var _alias_map: Dictionary = {}

## Maps span_id → tree_name for active spans.
## Built from span_open (which carries tree_name); used to route span_step/close/fail
## messages that do NOT include tree_name in their payloads.
var _span_tree_map: Dictionary[String, String] = {}

## Whether to call EngineDebugger.debug() on the next crash manifest.
## Intentionally NOT cleared in reset() so it survives game restarts and is
## re-sent to the game on every session_registered.
var auto_break: bool = false

## Hue index for golden-ratio peer color assignment.
var _color_index: int = 0

## Peer color table: tree_name → Color. Assigned once, never changed.
var _peer_colors: Dictionary[String, Color] = {}


# ─── Public API ───────────────────────────────────────────────────────────────

## Single entry point — called by [NetworkedDebuggerPlugin._capture].
func receive(message: String, data: Array) -> void:
	if data.is_empty():
		return
	var d: Dictionary = data[0] if data[0] is Dictionary else {}

	match message:
		"networked:session_registered":   _on_session_registered(d)
		"networked:session_unregistered": _on_session_unregistered(d)
		"networked:peer_connected":       _on_peer_event(d, true)
		"networked:peer_disconnected":    _on_peer_event(d, false)
		"networked:clock_sample":         _on_clock_sample(d)
		"networked:crash_manifest":       _on_crash_manifest(d)
		"networked:span_open":            _on_span(d, "open")
		"networked:span_step":            _on_span(d, "step")
		"networked:span_close":           _on_span(d, "close")
		"networked:span_fail":            _on_span(d, "fail")
		"networked:span_step_warn":       _on_span(d, "step_warn")
		"networked:lobby_event":          _on_lobby_event(d)
		"networked:topology_snapshot":    _on_topology_snapshot(d)


## Entry point for relay-forwarded messages — called by [NetworkedDebuggerPlugin._capture]
## when a [code]networked:relay_forward[/code] payload arrives.
##
## Wraps the data with [code]_is_remote = true[/code] so [method _on_session_registered]
## marks the peer correctly. Out-of-order messages (before session_registered) are
## buffered in [member _pending_remote] and drained when session_registered arrives.
func receive_remote(source_tree_name: String, message: String, data: Dictionary) -> void:
	if source_tree_name.is_empty():
		return
	var wrapped := data.duplicate()
	wrapped["_is_remote"] = true
	wrapped["tree_name"] = source_tree_name

	# Buffer if the peer isn't registered yet.
	if message != "networked:session_registered" and source_tree_name not in _peers:
		if source_tree_name not in _pending_remote:
			_pending_remote[source_tree_name] = []
		_pending_remote[source_tree_name].append({"msg": message, "data": wrapped})
		return

	receive(message, [wrapped])


## Returns a shallow copy of the peer registry for UI consumption.
func get_peers() -> Dictionary:
	return _peers.duplicate()


## Returns the adapter for [param key], or null if not found.
func get_adapter(key: String) -> PanelDataAdapter:
	return _adapters.get(key, null)


## Saves the auto-break state and sends it to the running game.
## Called by the Break on Manifest button inside any Crash Manifest PanelWrapper.
## State persists across game restarts — re-applied in [method _on_session_registered].
func set_auto_break(enabled: bool) -> void:
	auto_break = enabled
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [enabled])


## Asks the editor to open [param node_path] in the scene inspector.
## Only meaningful for [local] peers that have an active editor session.
func send_node_inspect(p_session_id: int, node_path: String) -> void:
	if plugin:
		plugin.send_to_game(p_session_id, "networked:inspect_node", [node_path])


## Sends a visualizer toggle command to the game for [param tree_name].
## Routed via the relay for remote peers automatically by the reporter.
func send_visualizer_toggle(tree_name: String, viz_name: String, enabled: bool) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:visualizer_toggle", [{
			"tree_name": tree_name,
			"viz_name": viz_name,
			"enabled": enabled,
		}])


## Wipes all ring buffers without touching the peer registry or colors.
## Called by the Clear button — peers remain visible in the left tree.
func clear_data() -> void:
	# Use the base clear() so data_changed fires — the UI handler returns early
	# when the buffer is empty, so open panels won't receive a spurious on_new_entry.
	for adapter: PanelDataAdapter in _adapters.values():
		adapter.clear()


## Wipes all state including peer registry. Called at the start of each new game run.
func reset() -> void:
	_peers.clear()
	_adapters.clear()
	_alias_map.clear()
	_span_tree_map.clear()
	_pending_remote.clear()
	_color_index = 0
	_peer_colors.clear()
	session_cleared.emit()


# ─── Message Handlers ─────────────────────────────────────────────────────────

func _on_session_registered(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if tn.is_empty():
		return

	var is_remote: bool = d.get("_is_remote", false)

	# Promotion: a direct session arrived for a peer we previously knew as [remote].
	if not is_remote and tn in _peers and _peers[tn].get("is_remote", false):
		_peers[tn]["is_remote"] = false
		_peers[tn]["online"] = true
		peer_promoted.emit(tn)
		return

	# Skip re-registration if already known (prevents duplicate tree items).
	if tn in _peers:
		return

	var is_server: bool = d.get("is_server", false)
	var color: Color = _assign_peer_color(tn)
	_peers[tn] = {
		"is_server":     is_server,
		"backend_class": d.get("backend_class", ""),
		"online":        true,
		"color":         color,
		# Server is always peer 1. Clients get their ID lazily from peer_connected.
		"peer_id":       1 if is_server else 0,
		"is_remote":     is_remote,
	}

	# Pre-create all adapters so ring buffers accumulate even before the user
	# opens a panel checkbox — for both local and remote peers.
	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CRASH,
		PanelDataAdapter.PanelType.TOPOLOGY,
	]:
		var key: String = _adapter_key(tn, pt)
		if key not in _adapters:
			_adapters[key] = _create_adapter(tn, pt)
			_adapters[key].data_changed.connect(
				func(k: String) -> void: adapter_data_changed.emit(k)
			)

	# Re-apply the persisted auto_break state now that this tree is live.
	# Only meaningful for local peers (remote peers receive via relay, not direct send).
	if plugin and not is_remote:
		plugin.send_to_game(session_id, "networked:set_auto_break", [auto_break])

	peer_registered.emit(tn, is_server, color, is_remote)

	# Drain any messages that arrived before session_registered for this peer.
	if tn in _pending_remote:
		var pending: Array = _pending_remote[tn]
		_pending_remote.erase(tn)
		for entry: Dictionary in pending:
			receive(entry["msg"], [entry["data"]])


func _on_session_unregistered(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if tn not in _peers:
		return
	_peers[tn]["online"] = false
	peer_status_changed.emit(tn, false)


func _on_peer_event(d: Dictionary, connected: bool) -> void:
	var tn: String = d.get("tree_name", "")
	if tn not in _peers:
		return

	_peers[tn]["online"] = connected
	peer_status_changed.emit(tn, connected)


func _on_clock_sample(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.CLOCK)
	if key not in _adapters:
		return
	(_adapters[key] as ClockAdapter).feed(d)


func _on_span(d: Dictionary, msg_type: String) -> void:
	var span_id: String = d.get("id", "")
	var tn: String = d.get("tree_name", "")

	if msg_type == "open":
		# span_open carries tree_name — record mapping for subsequent messages.
		if not span_id.is_empty() and not tn.is_empty():
			_span_tree_map[span_id] = tn
	else:
		# span_step / span_close / span_fail do NOT carry tree_name.
		# Resolve it from the map built at open time.
		if tn.is_empty() and span_id in _span_tree_map:
			tn = _span_tree_map[span_id]
		if msg_type == "close" or msg_type == "fail":
			_span_tree_map.erase(span_id)

	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.SPAN)
	if key not in _adapters:
		return
	(_adapters[key] as SpanAdapter).feed_span(d, msg_type)


func _on_crash_manifest(d: Dictionary) -> void:
	var ns: Dictionary = d.get("network_state", {})

	# Resolution order for tree_name:
	# 1. top-level "tree_name" field (added by _on_cpp_error_caught and C++ watchdog)
	# 2. network_state["tree_name"] (added by race-detection functions)
	# 3. reverse-lookup from network_state["peer_id"] in the peer registry
	# 4. single-peer fallback (common in solo-server sessions)
	var tn: String = d.get("tree_name", ns.get("tree_name", ""))

	if tn.is_empty():
		var pid: int = ns.get("peer_id", 0)
		if pid != 0:
			for candidate: String in _peers:
				if _peers[candidate].get("peer_id", -1) == pid:
					tn = candidate
					break

	if tn.is_empty() and _peers.size() == 1:
		tn = _peers.keys()[0]

	if tn.is_empty():
		return  # Cannot route — no registered peers yet

	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.CRASH)
	if key not in _adapters:
		return  # Session not fully initialized for this peer yet
	(_adapters[key] as CrashAdapter).feed(d)


func _on_lobby_event(d: Dictionary) -> void:
	## alias_map population: lobby_event carries lobby_name only, not the full
	## NodePath prefix needed for alias substitution. The map stays empty for now;
	## the field is reserved for a future reporter change that includes full paths.
	pass


func _on_topology_snapshot(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if tn.is_empty():
		return
	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.TOPOLOGY)
	if key not in _adapters:
		return
	(_adapters[key] as TopologyAdapter).feed(d)


# ─── Adapter Factory ──────────────────────────────────────────────────────────

func _create_adapter(tn: String, pt: PanelDataAdapter.PanelType) -> PanelDataAdapter:
	match pt:
		PanelDataAdapter.PanelType.CLOCK:
			return ClockAdapter.new(tn)
		PanelDataAdapter.PanelType.SPAN:
			return SpanAdapter.new(tn)
		PanelDataAdapter.PanelType.CRASH:
			return CrashAdapter.new(tn, _alias_map)
		PanelDataAdapter.PanelType.TOPOLOGY:
			return TopologyAdapter.new(tn)
	return PanelDataAdapter.new()


# ─── Peer Color ───────────────────────────────────────────────────────────────

## Assigns a stable HSV color to [param tree_name] using the golden-ratio hue shift.
## Returns the existing color if one was already assigned.
func _assign_peer_color(tree_name: String) -> Color:
	if tree_name in _peer_colors:
		return _peer_colors[tree_name]
	var hue: float = fmod(float(_color_index) * 0.618033988749895, 1.0)
	var color := Color.from_hsv(hue, 0.65, 0.85)
	_peer_colors[tree_name] = color
	_color_index += 1
	return color


# ─── Key Scheme ───────────────────────────────────────────────────────────────

## Stable adapter key: [code]"tree_name:panel_name"[/code]
func _adapter_key(tn: String, pt: PanelDataAdapter.PanelType) -> String:
	return "%s:%s" % [tn, PanelDataAdapter.PANEL_NAMES[pt]]
