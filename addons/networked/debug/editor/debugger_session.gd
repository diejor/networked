@tool
class_name DebuggerSession
extends RefCounted

## Emitted when a new MultiplayerTree peer completes registration.
signal peer_registered(peer_key: String, display_name: String, is_server: bool, color: Color, is_remote: bool, peer_id: int)

## Emitted when a peer's online status changes.
signal peer_status_changed(peer_key: String, online: bool)

## Emitted when a peer's peer_id is resolved from 0 to its real value.
signal peer_id_resolved(peer_key: String, peer_id: int)

## Emitted after an adapter appends a new entry to its ring buffer.
signal adapter_data_changed(adapter_key: String)

## Emitted after [method reset] wipes all state.
signal session_cleared()

## Injected by [NetworkedDebuggerPlugin] at construction time.
var plugin: EditorDebuggerPlugin
var session_id: int

## Peer registry: peer_key → {display_name, is_server, backend_class, online, color, peer_id}
## peer_key = "source_path|reporter_id" — guaranteed unique across processes.
var _peers: Dictionary[String, Dictionary] = {}

## All adapters: adapter_key → PanelDataAdapter subclass instance.
var _adapters: Dictionary[String, PanelDataAdapter] = {}

## Alias map shared by all [CrashAdapter] instances (NodePath prefix → readable alias).
var _alias_map: Dictionary = {}

## Map of span_id -> peer_key for routing span events that arrive without context.
var _span_peer_map: Dictionary = {}

## Deduplication set for crash manifests replayed via snapshot.
## peer_key -> Dictionary[cid, true] — prevents the same manifest appearing twice
## when both the live event and the history replay arrive in the same session.
var _seen_crash_cids: Dictionary = {}

var auto_break: bool = false

## Hue index for golden-ratio peer color assignment.
var _color_index: int = 0

## Peer color table: peer_id → Color (stable by peer_id for consistent coloring).
var _peer_colors: Dictionary[int, Color] = {}


# ─── Public API ───────────────────────────────────────────────────────────────

## Single entry point — called by [NetworkedDebuggerPlugin._capture].
## [param data] is either:
## - A single-element Array containing the raw [PackedByteArray] envelope bytes
##   (for "networked:envelope"), OR
## - A control message with non-envelope payload (e.g. "networked:relay_disconnected").
func receive(message: String, data: Array) -> void:
	# Control messages that carry no envelope — handle before the empty-data guard.
	if message == "networked:relay_disconnected":
		mark_remote_peers_offline()
		return

	if data.is_empty():
		return

	if data[0] is PackedByteArray:
		var envelope := NetEnvelope.from_dict(bytes_to_var(data[0]))
		var is_remote := message == "networked:envelope_remote"
		_route_envelope(envelope, is_remote)
		return

	# Fallback: legacy message handling for any pre-envelope path still in flight.
	NetLog.warn("DebuggerSession: [LegacyMessage] %s — expected envelope" % message)


func get_peers() -> Dictionary:
	return _peers.duplicate()


func get_adapter(key: String) -> PanelDataAdapter:
	return _adapters.get(key, null)


func set_auto_break(enabled: bool) -> void:
	auto_break = enabled
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [enabled])


func send_node_inspect(p_session_id: int, node_path: String) -> void:
	if plugin:
		plugin.send_to_game(p_session_id, "networked:inspect_node", [node_path])


func send_visualizer_toggle(peer_key: String, node_path: String, viz_name: String, enabled: bool) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:visualizer_toggle", [{
			"peer_key": peer_key,
			"node_path": node_path,
			"viz_name": viz_name,
			"enabled": enabled,
		}])


func clear_data() -> void:
	for adapter: PanelDataAdapter in _adapters.values():
		adapter.clear()


func reset() -> void:
	_peers.clear()
	_adapters.clear()
	_alias_map.clear()
	_span_peer_map.clear()
	_seen_crash_cids.clear()
	_color_index = 0
	_peer_colors.clear()
	session_cleared.emit()


## Marks every registered peer as offline without clearing data.
## Called when the game session stops so the last snapshot is preserved for inspection.
func mark_all_offline() -> void:
	for pk: String in _peers.keys():
		if _peers[pk].get("online", false):
			_peers[pk]["online"] = false
			peer_status_changed.emit(pk, false)


## Marks only remote peers as offline.
## Called when the relay server disconnects — local peers (direct EngineDebugger path)
## are unaffected; only relay-forwarded peers become unreachable.
func mark_remote_peers_offline() -> void:
	for pk: String in _peers.keys():
		if _peers[pk].get("is_remote", false) and _peers[pk].get("online", false):
			_peers[pk]["online"] = false
			peer_status_changed.emit(pk, false)


# ─── Envelope Router ──────────────────────────────────────────────────────────

func _route_envelope(envelope: NetEnvelope, is_remote: bool = false) -> void:
	if envelope.source_path.is_empty() or envelope.reporter_id.is_empty():
		NetLog.warn("DebuggerSession: [DropBadEnvelope] missing source_path or reporter_id")
		return

	var pk := envelope.peer_key()

	# SAFETY-NET: prefer direct (local) telemetry over relayed telemetry.
	# Same-process clients emit via EngineDebugger before any relay path can echo the packet.
	# Cheap to keep as a final guard even after the relay correctly prevents echoes.
	if is_remote and pk in _peers and not _peers[pk].get("is_remote", true):
		return

	NetLog.trace("DebuggerSession: [Route] %s from %s (remote=%s)" % [envelope.msg, pk, is_remote])

	match envelope.msg:
		"networked:session_registered":   _on_session_registered(envelope, is_remote)
		"networked:session_unregistered": _on_session_unregistered(envelope)
		"networked:peer_connected":       _on_peer_event(envelope, true)
		"networked:peer_disconnected":    _on_peer_event(envelope, false)
		"networked:clock_sample":         _on_clock_sample(envelope)
		"networked:crash_manifest":       _on_crash_manifest(envelope)
		"networked:span_open":            _on_span(envelope, "open")
		"networked:span_step":            _on_span(envelope, "step")
		"networked:span_close":           _on_span(envelope, "close")
		"networked:span_fail":            _on_span(envelope, "fail")
		"networked:span_step_warn":       _on_span(envelope, "step_warn")
		"networked:lobby_event":          _on_lobby_event(envelope)
		"networked:topology_snapshot":    _on_topology_snapshot(envelope)
		_:
			NetLog.trace("DebuggerSession: [UnknownMsg] %s" % envelope.msg)


# ─── Message Handlers ─────────────────────────────────────────────────────────

func _on_session_registered(envelope: NetEnvelope, is_remote: bool = false) -> void:
	var pk := envelope.peer_key()
	var d := envelope.payload
	var display := envelope.display_name()
	var peer_id := envelope.peer_id
	var is_server: bool = d.get("is_server", false) or peer_id == 1

	if is_server:
		display = "Server"

	# If already online, resolve peer_id if it was unknown (0) at registration time.
	if pk in _peers and _peers[pk].get("online", false):
		# Upgrade path: if we were remote but just got a local registration, prefer local.
		if not is_remote and _peers[pk].get("is_remote", false):
			_peers[pk]["is_remote"] = false
			NetLog.info("DebuggerSession: [UpgradeToLocal] '%s'" % display)

		if _peers[pk].get("peer_id", 0) == 0 and peer_id != 0:
			_peers[pk]["peer_id"] = peer_id
			peer_id_resolved.emit(pk, peer_id)
		NetLog.debug("DebuggerSession: [IgnoreDuplicate] for '%s'" % display)
		return

	NetLog.info("DebuggerSession: [Registered] '%s' (peer=%d, remote=%s, path=%s)" % [display, peer_id, is_remote, envelope.source_path])

	var color: Color = _assign_peer_color(peer_id)
	_peers[pk] = {
		"display_name": display,
		"is_server": is_server,
		"backend_class": d.get("backend_class", ""),
		"online": true,
		"color": color,
		"peer_id": peer_id,
		"is_remote": is_remote,
	}

	peer_registered.emit(pk, display, is_server, color, is_remote, peer_id)

	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CRASH,
		PanelDataAdapter.PanelType.TOPOLOGY,
	]:
		var key: String = _adapter_key(pk, pt)
		if key not in _adapters:
			_adapters[key] = _create_adapter(pk, display, pt)
			_adapters[key].data_changed.connect(
				func(k: String) -> void: adapter_data_changed.emit(k)
			)

	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [auto_break])


func _on_session_unregistered(envelope: NetEnvelope) -> void:
	var pk := envelope.peer_key()
	if pk not in _peers: return
	_peers[pk]["online"] = false
	peer_status_changed.emit(pk, false)


func _on_peer_event(envelope: NetEnvelope, connected: bool) -> void:
	var pk := envelope.peer_key()
	if pk not in _peers: return
	if connected:
		_peers[pk]["peer_id"] = envelope.peer_id

	for pt in [PanelDataAdapter.PanelType.CLOCK, PanelDataAdapter.PanelType.SPAN, PanelDataAdapter.PanelType.CRASH, PanelDataAdapter.PanelType.TOPOLOGY]:
		var key := _adapter_key(pk, pt)
		if key in _adapters:
			_adapters[key].on_peer_event(envelope.payload, connected)


func _on_clock_sample(envelope: NetEnvelope) -> void:
	var key := _adapter_key(envelope.peer_key(), PanelDataAdapter.PanelType.CLOCK)
	if key in _adapters:
		_adapters[key].feed(envelope.payload)


func _on_crash_manifest(envelope: NetEnvelope) -> void:
	var pk := envelope.peer_key()
	var cid: String = envelope.payload.get("cid", "")
	# DEDUP-3 (replay idempotency): crash manifests are replayed to late-joining editors
	# via _emit_current_state(). This prevents a manifest appearing twice when both the
	# live event and the history replay arrive in the same editor session.
	if not cid.is_empty():
		if pk not in _seen_crash_cids:
			_seen_crash_cids[pk] = {}
		if cid in _seen_crash_cids[pk]:
			return
		_seen_crash_cids[pk][cid] = true
	var key := _adapter_key(pk, PanelDataAdapter.PanelType.CRASH)
	if key in _adapters:
		_adapters[key].feed(envelope.payload)


func _on_span(envelope: NetEnvelope, type: String) -> void:
	var pk := envelope.peer_key()
	var span_id: String = envelope.payload.get("id", "")
	if pk.is_empty() and span_id in _span_peer_map:
		pk = _span_peer_map[span_id]
	if not pk.is_empty() and not span_id.is_empty():
		_span_peer_map[span_id] = pk
	var key := _adapter_key(pk, PanelDataAdapter.PanelType.SPAN)
	if key in _adapters:
		_adapters[key].on_span_event(envelope.payload, type)


func _on_lobby_event(envelope: NetEnvelope) -> void:
	var np: String = envelope.payload.get("node_path", "")
	var alias: String = envelope.payload.get("alias", "")
	if not np.is_empty() and not alias.is_empty():
		_alias_map[np] = alias
	var key := _adapter_key(envelope.peer_key(), PanelDataAdapter.PanelType.SPAN)
	if key in _adapters:
		_adapters[key].feed(envelope.payload)


func _on_topology_snapshot(envelope: NetEnvelope) -> void:
	var key := _adapter_key(envelope.peer_key(), PanelDataAdapter.PanelType.TOPOLOGY)
	if key in _adapters:
		_adapters[key].feed(envelope.payload)


# ─── Internal ─────────────────────────────────────────────────────────────────

func _adapter_key(peer_k: String, type: PanelDataAdapter.PanelType) -> String:
	return "%s:%s" % [peer_k, PanelDataAdapter.PANEL_NAMES[type]]


func _create_adapter(peer_k: String, display: String, type: PanelDataAdapter.PanelType) -> PanelDataAdapter:
	var adapter: PanelDataAdapter
	match type:
		PanelDataAdapter.PanelType.CLOCK:
			adapter = ClockAdapter.new(display)
		PanelDataAdapter.PanelType.SPAN:
			adapter = SpanAdapter.new(display)
		PanelDataAdapter.PanelType.CRASH:
			adapter = CrashAdapter.new(display, _alias_map)
		PanelDataAdapter.PanelType.TOPOLOGY:
			adapter = TopologyAdapter.new(display)
	# Override the adapter_key to use peer_k (unique) not just display name
	adapter.adapter_key = "%s:%s" % [peer_k, PanelDataAdapter.PANEL_NAMES[type]]
	return adapter


func _assign_peer_color(peer_id: int) -> Color:
	if peer_id in _peer_colors: return _peer_colors[peer_id]

	var h := fmod(float(abs(peer_id)) * 0.618033988749895, 1.0)
	var c := Color.from_hsv(h, 0.6, 0.9)
	_peer_colors[peer_id] = c
	return c
