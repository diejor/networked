@tool
class_name DebuggerSession
extends RefCounted

## Emitted when a new MultiplayerTree peer completes registration.
signal peer_registered(tree_name: String, is_server: bool, color: Color, is_remote: bool)

## Emitted when a peer's online status changes.
signal peer_status_changed(tree_name: String, online: bool)

## Emitted when a previously [remote] peer gains a direct editor connection and
## is promoted to [local].
signal peer_promoted(tree_name: String)


## Emitted after an adapter appends a new entry to its ring buffer.
signal adapter_data_changed(adapter_key: String)

## Emitted after [method reset] wipes all state.
signal session_cleared()

## Injected by [NetworkedDebuggerPlugin] at construction time.
var plugin: EditorDebuggerPlugin
var session_id: int

## Peer registry: tree_name → {is_server, backend_class, online, color, peer_id, is_remote, rid}
var _peers: Dictionary[String, Dictionary] = {}

## Mapping of (original_name, is_remote, rid) -> final_renamed_name
var _name_map: Dictionary = {}

## All adapters: adapter_key → PanelDataAdapter subclass instance.
var _adapters: Dictionary[String, PanelDataAdapter] = {}

## Alias map shared by all [CrashAdapter] instances (NodePath prefix → readable alias).
var _alias_map: Dictionary = {}

## Map of span_id -> tree_name for routing span events that arrive without context.
var _span_tree_map: Dictionary = {}

## Tracks reporter IDs of local processes to filter out echos from the relay.
var _local_rids: Dictionary = {} # rid -> bool

var auto_break: bool = false

## Hue index for golden-ratio peer color assignment.
var _color_index: int = 0

## Peer color table: peer_id → Color.
var _peer_colors: Dictionary[int, Color] = {}


# ─── Public API ───────────────────────────────────────────────────────────────

## Single entry point — called by [NetworkedDebuggerPlugin._capture].
func receive(message: String, data: Array, is_remote: bool = false) -> void:
	if data.is_empty():
		return
	var d: Dictionary = data[0] if data[0] is Dictionary else {}
	
	# Resolve IDs for echo filtering and mapping.
	var rid: String = d.get("_rid", "")
	var tn: String = d.get("tree_name", "")
	var peer_id: int = int(d.get("peer_id", 0))
	
	if not is_remote and not rid.is_empty():
		if rid not in _local_rids:
			NetLog.debug("DebuggerSession: [DetectedLocalRID] %s" % rid.left(4))
		_local_rids[rid] = true

	# Resolve the final unique name for this peer.
	var final_tn := _get_mapped_name(tn, is_remote, rid)
	
	# Patch the dictionary so all handlers see the mapped name.
	if final_tn != tn:
		d = d.duplicate()
		d["tree_name"] = final_tn

	if message != "networked:relay_forward":
		NetLog.trace("DebuggerSession: [Receive] %s (tree=%s, peer=%d, rid=%s)" % [message, final_tn, peer_id, rid.left(4)])

	match message:
		"networked:session_registered":   _on_session_registered(d, is_remote)
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
		"networked:relay_forward":
			# Relay payload: [ { msg, data, source_tree_name } ]
			for entry in data:
				receive_remote(entry.get("source_tree_name", ""), entry.get("msg", ""), entry.get("data", {}))


## Entry point for relay-forwarded messages.
func receive_remote(source_tree_name: String, message: String, data: Dictionary) -> void:
	if source_tree_name.is_empty():
		return
	
	var rid: String = data.get("_rid", "")
	if not rid.is_empty() and rid in _local_rids:
		NetLog.debug("DebuggerSession: [IgnoreEcho] %s from %s (%s)" % [message, source_tree_name, rid.left(4)])
		return

	NetLog.trace("DebuggerSession: [ReceiveRemote] %s from %s (rid=%s)" % [message, source_tree_name, rid.left(4)])
	var patched := data.duplicate()
	patched["tree_name"] = source_tree_name

	receive(message, [patched], true)


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


func send_visualizer_toggle(tree_name: String, viz_name: String, enabled: bool) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:visualizer_toggle", [{
			"tree_name": tree_name,
			"viz_name": viz_name,
			"enabled": enabled,
		}])


func clear_data() -> void:
	for adapter: PanelDataAdapter in _adapters.values():
		adapter.clear()


func reset() -> void:
	_peers.clear()
	_name_map.clear()
	_local_rids.clear()
	_adapters.clear()
	_alias_map.clear()
	_span_tree_map.clear()
	_color_index = 0
	_peer_colors.clear()
	session_cleared.emit()


# ─── Message Handlers ─────────────────────────────────────────────────────────

func _on_session_registered(d: Dictionary, is_remote: bool = false) -> void:
	var tn: String = d.get("tree_name", "")
	if tn.is_empty(): return
	
	var rid: String = d.get("_rid", "")
	var peer_id: int = int(d.get("peer_id", 0))
	var is_server: bool = d.get("is_server", false)

	# Deduplicate and differentiate:
	var final_tn := tn
	
	if is_server or peer_id == 1:
		final_tn = "Server" # Always anchor peer 1 as 'Server'
	
	if not is_remote:
		# LOCAL peers: rename if concurrent name collision.
		var counter := 2
		while _is_name_taken(final_tn, rid):
			final_tn = "%s (%d)" % [tn, counter]
			counter += 1
		
		if final_tn in _peers and not _peers[final_tn].get("online", false):
			NetLog.info("DebuggerSession: [Reusing] offline slot for '%s'" % final_tn)
		elif final_tn != tn and not is_server:
			NetLog.info("DebuggerSession: [Rename] concurrent local tree '%s' -> '%s'" % [tn, final_tn])
	else:
		# REMOTE peers: ensure uniqueness among all registered peers.
		if _is_name_taken(final_tn, rid):
			if not _peers[final_tn].get("is_remote", false):
				final_tn = tn + " [remote]"
			
			var counter := 2
			var base_remote_name := final_tn
			while _is_name_taken(final_tn, rid):
				final_tn = "%s (%d)" % [base_remote_name, counter]
				counter += 1

	# Register the mapping so future messages from this source find the right peer.
	_name_map[[tn, is_remote, rid]] = final_tn

	# Promotion check:
	if not is_remote and final_tn in _peers and _peers[final_tn].get("is_remote", false):
		NetLog.info("DebuggerSession: [Promoting] remote peer '%s' to local" % final_tn)
		_promote_peer(final_tn)
		return

	# Skip if already known and online.
	if final_tn in _peers and _peers[final_tn].get("online", false):
		NetLog.debug("DebuggerSession: [IgnoreDuplicate] for '%s'" % final_tn)
		return

	NetLog.info("DebuggerSession: [Registered] '%s' (peer=%d, is_remote=%s)" % [final_tn, peer_id, is_remote])

	var color: Color = _assign_peer_color(peer_id)
	_peers[final_tn] = {
		"is_server": is_server,
		"backend_class": d.get("backend_class", ""),
		"online": true,
		"color": color,
		"peer_id": peer_id,
		"is_remote": is_remote,
		"rid": rid,
	}

	peer_registered.emit(final_tn, is_server, color, is_remote)

	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CRASH,
		PanelDataAdapter.PanelType.TOPOLOGY,
	]:
		var key: String = _adapter_key(final_tn, pt)
		if key not in _adapters:
			_adapters[key] = _create_adapter(final_tn, pt)
			_adapters[key].data_changed.connect(
				func(k: String) -> void: adapter_data_changed.emit(k)
			)

	if plugin and not is_remote:
		plugin.send_to_game(session_id, "networked:set_auto_break", [auto_break])


func _promote_peer(tn: String) -> void:
	if tn in _peers:
		_peers[tn]["is_remote"] = false
		_peers[tn]["online"] = true
		peer_promoted.emit(tn)


func _on_session_unregistered(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	var rid: String = d.get("_rid", "")
	var final_tn := _get_mapped_name(tn, false, rid)
	
	if final_tn not in _peers: return
	_peers[final_tn]["online"] = false
	peer_status_changed.emit(final_tn, false)


func _on_peer_event(d: Dictionary, connected: bool) -> void:
	var tn: String = d.get("tree_name", "")
	if tn not in _peers: return
	if connected:
		_peers[tn]["peer_id"] = d.get("peer_id", 0)
	
	for pt in [PanelDataAdapter.PanelType.CLOCK, PanelDataAdapter.PanelType.SPAN, PanelDataAdapter.PanelType.CRASH, PanelDataAdapter.PanelType.TOPOLOGY]:
		var key := _adapter_key(tn, pt)
		if key in _adapters:
			_adapters[key].on_peer_event(d, connected)


func _on_clock_sample(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	var key := _adapter_key(tn, PanelDataAdapter.PanelType.CLOCK)
	if key in _adapters:
		_adapters[key].feed(d)


func _on_crash_manifest(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	var key := _adapter_key(tn, PanelDataAdapter.PanelType.CRASH)
	if key in _adapters:
		_adapters[key].feed(d)


func _on_span(d: Dictionary, type: String) -> void:
	var tn: String = d.get("tree_name", "")
	var span_id: String = d.get("id", "")
	if tn.is_empty() and span_id in _span_tree_map:
		tn = _span_tree_map[span_id]
	if not tn.is_empty() and not span_id.is_empty():
		_span_tree_map[span_id] = tn
	var key := _adapter_key(tn, PanelDataAdapter.PanelType.SPAN)
	if key in _adapters:
		_adapters[key].on_span_event(d, type)


func _on_lobby_event(d: Dictionary) -> void:
	var np: String = d.get("node_path", "")
	var alias: String = d.get("alias", "")
	if not np.is_empty() and not alias.is_empty():
		_alias_map[np] = alias
	var tn: String = d.get("tree_name", "")
	var key := _adapter_key(tn, PanelDataAdapter.PanelType.SPAN)
	if key in _adapters:
		_adapters[key].feed(d)


func _on_topology_snapshot(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	var key := _adapter_key(tn, PanelDataAdapter.PanelType.TOPOLOGY)
	if key in _adapters:
		_adapters[key].feed(d)


# ─── Internal ─────────────────────────────────────────────────────────────────

func _get_mapped_name(original_name: String, is_remote: bool, rid: String = "") -> String:
	if not rid.is_empty() and _name_map.has([original_name, is_remote, rid]):
		return _name_map[[original_name, is_remote, rid]]
	return _name_map.get([original_name, is_remote], original_name)


func _is_name_taken(p_name: String, p_rid: String) -> bool:
	if p_name not in _peers: return false
	if not _peers[p_name].get("online", false): return false
	return _peers[p_name].get("rid", "") != p_rid


func _adapter_key(tree_name: String, type: PanelDataAdapter.PanelType) -> String:
	return "%s:%s" % [tree_name, PanelDataAdapter.PANEL_NAMES[type]]


func _create_adapter(tree_name: String, type: PanelDataAdapter.PanelType) -> PanelDataAdapter:
	var adapter: PanelDataAdapter
	match type:
		PanelDataAdapter.PanelType.CLOCK:
			adapter = ClockAdapter.new(tree_name)
		PanelDataAdapter.PanelType.SPAN:
			adapter = SpanAdapter.new(tree_name)
		PanelDataAdapter.PanelType.CRASH:
			adapter = CrashAdapter.new(tree_name, _alias_map)
		PanelDataAdapter.PanelType.TOPOLOGY:
			adapter = TopologyAdapter.new(tree_name)
	return adapter


func _assign_peer_color(peer_id: int) -> Color:
	if peer_id in _peer_colors: return _peer_colors[peer_id]
	
	if peer_id == 1:
		# Server is always a consistent gold/cyan.
		var c := Color(0.2, 0.8, 1.0) # Cyan
		_peer_colors[peer_id] = c
		return c
	
	# Clients: Deterministic hash based on Peer ID.
	var h := fmod(float(abs(peer_id)) * 0.618033988749895, 1.0)
	var c := Color.from_hsv(h, 0.6, 0.9)
	_peer_colors[peer_id] = c
	return c
