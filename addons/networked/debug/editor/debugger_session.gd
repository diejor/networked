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
signal peer_registered(tree_name: String, is_server: bool, color: Color)

## Emitted when a peer's online status changes.
signal peer_status_changed(tree_name: String, online: bool)


## Emitted after an adapter appends a new entry to its ring buffer.
signal adapter_data_changed(adapter_key: String)

## Emitted after [method reset] wipes all state.
signal session_cleared()

## Injected by [NetworkedDebuggerPlugin] at construction time.
var plugin: NetworkedDebuggerPlugin
var session_id: int

## Peer registry: tree_name → {is_server, backend_class, online, color, peer_id}
## peer_id is 0 until the first peer_connected/disconnected message for this tree.
var _peers: Dictionary[String, Dictionary] = {}

## All adapters: adapter_key → PanelDataAdapter subclass instance.
var _adapters: Dictionary[String, PanelDataAdapter] = {}

## Alias map shared by all [CrashAdapter] instances (NodePath prefix → readable alias).
## Populated by networked:lobby_event messages; passed by reference to each CrashAdapter.
var _alias_map: Dictionary = {}

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
		"networked:lobby_event":          _on_lobby_event(d)


## Returns a shallow copy of the peer registry for UI consumption.
func get_peers() -> Dictionary:
	return _peers.duplicate()


## Returns the adapter for [param key], or null if not found.
func get_adapter(key: String) -> PanelDataAdapter:
	return _adapters.get(key, null)


## Sends the auto-break toggle to the running game for the given session.
## Called by the Break on Manifest button inside a Crash Manifest PanelWrapper.
func set_auto_break(enabled: bool) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [enabled])


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
	_color_index = 0
	_peer_colors.clear()
	session_cleared.emit()


# ─── Message Handlers ─────────────────────────────────────────────────────────

func _on_session_registered(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if tn.is_empty():
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
	}

	# Pre-create all three adapters so ring buffers accumulate even before
	# the user opens a panel checkbox.
	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CRASH,
	]:
		var key: String = _adapter_key(tn, pt)
		if key not in _adapters:
			_adapters[key] = _create_adapter(tn, pt)
			_adapters[key].data_changed.connect(
				func(k: String) -> void: adapter_data_changed.emit(k)
			)

	# Push the current auto-break state now that the game session is live.
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [false])

	peer_registered.emit(tn, is_server, color)


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
	var tn: String = d.get("tree_name", "")
	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.SPAN)
	if key not in _adapters:
		return
	(_adapters[key] as SpanAdapter).feed_span(d, msg_type)


func _on_crash_manifest(d: Dictionary) -> void:
	var ns: Dictionary = d.get("network_state", {})
	var tn: String = ns.get("tree_name", d.get("tree_name", ""))
	var key: String = _adapter_key(tn, PanelDataAdapter.PanelType.CRASH)
	if key not in _adapters:
		# If the crash arrived before session_registered (rare), create adapters now.
		if tn.is_empty():
			return
		_assign_peer_color(tn)
		_peers[tn] = {"is_server": false, "online": false, "color": _peer_colors[tn], "peer_id": 0}
		for pt: PanelDataAdapter.PanelType in [
			PanelDataAdapter.PanelType.CLOCK,
			PanelDataAdapter.PanelType.SPAN,
			PanelDataAdapter.PanelType.CRASH,
		]:
			var k: String = _adapter_key(tn, pt)
			if k not in _adapters:
				_adapters[k] = _create_adapter(tn, pt)
				_adapters[k].data_changed.connect(
					func(ak: String) -> void: adapter_data_changed.emit(ak)
				)
	(_adapters[key] as CrashAdapter).feed(d)


func _on_lobby_event(d: Dictionary) -> void:
	## alias_map population: lobby_event carries lobby_name only, not the full
	## NodePath prefix needed for alias substitution. The map stays empty for now;
	## the field is reserved for a future reporter change that includes full paths.
	pass


# ─── Adapter Factory ──────────────────────────────────────────────────────────

func _create_adapter(tn: String, pt: PanelDataAdapter.PanelType) -> PanelDataAdapter:
	match pt:
		PanelDataAdapter.PanelType.CLOCK:
			return ClockAdapter.new(tn)
		PanelDataAdapter.PanelType.SPAN:
			return SpanAdapter.new(tn)
		PanelDataAdapter.PanelType.CRASH:
			return CrashAdapter.new(tn, _alias_map)
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
