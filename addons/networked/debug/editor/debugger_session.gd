@tool
class_name DebuggerSession
extends RefCounted

## Emitted when a new [MultiplayerTree] peer completes registration.
signal peer_registered(peer_key: String, display_name: String, tree_name: String, is_server: bool, color: Color, is_remote: bool, peer_id: int)

## Emitted when a peer is removed from the registry.
signal peer_unregistered(peer_key: String)

## Emitted when a peer's identity (username) is updated.
signal peer_identity_changed(peer_key: String, username: String)

## Emitted when a peer's online status changes.
signal peer_status_changed(peer_key: String, online: bool)

## Emitted when a peer's [member peer_id] is resolved from [code]0[/code] to its real value.
signal peer_id_resolved(peer_key: String, peer_id: int)

## Emitted after an adapter appends a new entry to its ring buffer.
signal adapter_data_changed(adapter_key: String)

## Emitted after [method reset] wipes all state.
signal session_cleared()

## Injected by [NetworkedDebuggerPlugin] at construction time.
var plugin: EditorDebuggerPlugin
var session_id: int

## Peer registry: [code]peer_key[/code] -> [Dictionary]
## [br][br]
## [code]peer_key[/code] = [code]"source_path|reporter_id"[/code] - guaranteed unique across processes.
var _peers: Dictionary[String, Dictionary] = {}

## All adapters: [code]adapter_key[/code] -> [PanelDataAdapter] instance.
var _adapters: Dictionary[String, PanelDataAdapter] = {}

## Alias map shared by all [CrashAdapter] instances (NodePath prefix -> readable alias).
var _alias_map: Dictionary = {}

## Map of [code]span_id[/code] -> [code]peer_key[/code] for routing span events that arrive without context.
var _span_peer_map: Dictionary = {}

## Deduplication set for crash manifests replayed via snapshot.
## [br][br]
## [code]peer_key[/code] -> [code]Dictionary[cid, true][/code] - prevents the same manifest appearing twice
## when both the live event and the history replay arrive in the same session.
var _seen_crash_cids: Dictionary = {}

var auto_break: bool = false

## Hue index for golden-ratio peer color assignment.
var _color_index: int = 0

## Peer color table: [code]peer_id[/code] -> [Color] (stable by [code]peer_id[/code] for consistent coloring).
var _peer_colors: Dictionary[int, Color] = {}


# ─── Public API ───────────────────────────────────────────────────────────────

## Single entry point - called by [method NetworkedDebuggerPlugin._capture].
## [br][br]
## [param data] is either:
## [br]- A single-element [Array] containing raw [PackedByteArray] envelope bytes.
## [br]- A control message with non-envelope payload.
func receive(message: String, data: Array, is_remote: bool = false) -> void:
	if data.is_empty():
		return

	if data[0] is PackedByteArray:
		var envelope := NetEnvelope.from_dict(bytes_to_var(data[0]))
		_route_envelope(envelope, is_remote)
		return

	# Fallback: legacy message handling for any pre-envelope path in flight.
	var warn_msg := "DebuggerSession: [LegacyMessage] %s - expected envelope"
	Netw.dbg.warn(warn_msg % [message], func(m): push_warning(m))


func get_peers() -> Dictionary:
	return _peers.duplicate()


func get_adapter(key: String) -> PanelDataAdapter:
	return _adapters.get(key, null)


func set_auto_break(enabled: bool) -> void:
	auto_break = enabled
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [enabled])


func send_node_inspect(
	peer_key: String, 
	node_path: String, 
	peer_id: int = 0
) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:inspect_node", [{
			"peer_key": peer_key,
			"node_path": node_path,
			"peer_id": peer_id,
		}])


func send_visualizer_toggle(
	peer_key: String, 
	node_path: String, 
	viz_name: String, 
	enabled: bool,
	peer_id: int = 0
) -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:visualizer_toggle", [{
			"peer_key": peer_key,
			"node_path": node_path,
			"viz_name": viz_name,
			"enabled": enabled,
			"peer_id": peer_id,
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
## [br][br]
## [b]Note:[/b] Called when the game session stops so the last snapshot is
## preserved for inspection.
func mark_all_offline() -> void:
	for pk: String in _peers.keys():
		if _peers[pk].get("online", false):
			_peers[pk]["online"] = false
			peer_status_changed.emit(pk, false)


## Marks specific peers identified by [param peer_keys] as offline.
## [br][br]
## [b]Note:[/b] Called by [NetworkedDebuggerPlugin] when a session stops to
## update other active sessions that were relaying its peers.
func mark_peers_offline(peer_keys: Array[String]) -> void:
	for pk in peer_keys:
		if pk in _peers and _peers[pk].get("online", false):
			_peers[pk]["online"] = false
			peer_status_changed.emit(pk, false)


## Removes specific peers identified by [param peer_keys] from the registry.
## [br][br]
## [b]Note:[/b] This is called by [NetworkedDebuggerPlugin] when a session
## restarts to clear stale remote references in other sessions.
func unregister_peers(peer_keys: Array[String]) -> void:
	for pk in peer_keys:
		if pk in _peers:
			_peers.erase(pk)

			# Identify and remove all adapters associated with this peer.
			var prefix := pk + ":"
			var to_erase: Array[String] = []
			for a_key in _adapters:
				if a_key.begins_with(prefix):
					to_erase.append(a_key)

			for a_key in to_erase:
				_adapters.erase(a_key)

			peer_unregistered.emit(pk)


# ─── Envelope Router ──────────────────────────────────────────────────────────

func _route_envelope(envelope: NetEnvelope, is_remote: bool = false) -> void:
	if envelope.source_path.is_empty() or envelope.reporter_id.is_empty():
		Netw.dbg.warn("DebuggerSession: [DropBadEnvelope] missing source_path or reporter_id")
		return

	var pk := envelope.peer_key()

	# SAFETY-NET: prefer direct (local) telemetry over relayed telemetry.
	# Same-process clients emit via EngineDebugger before the editor-side
	# relay can echo the packet to this tab.
	if is_remote and pk in _peers and not _peers[pk].get("is_remote", true):
		return

	_trace_route(envelope.msg, pk, is_remote)

	match envelope.msg:
		"networked:session_registered":   _on_session_registered(envelope, is_remote)
		"networked:session_unregistered": _on_session_unregistered(envelope, is_remote)
		"networked:peer_connected":       _on_peer_event(envelope, true)
		"networked:peer_disconnected":    _on_peer_event(envelope, false)
		"networked:clock_sample":         _on_clock_sample(envelope, is_remote)
		"networked:crash_manifest":       _on_crash_manifest(envelope)

		"networked:span_open":            _on_span(envelope, "open")
		"networked:span_step":            _on_span(envelope, "step")
		"networked:span_close":           _on_span(envelope, "close")
		"networked:span_fail":            _on_span(envelope, "close")
		"networked:span_step_warn":       _on_span(envelope, "close")
		"networked:lobby_event":          _on_lobby_event(envelope)
		"networked:topology_snapshot":    _on_topology_snapshot(envelope)
		_:
			Netw.dbg.trace("DebuggerSession: [UnknownMsg] %s" % [envelope.msg])


func _trace_route(msg: String, pk: String, is_remote: bool) -> void:
	# Filter heartbeats and other high-frequency traffic that floods the trace.
	if msg == "networked:clock_sample" or msg.begins_with("networked:span_"):
		return

	Netw.dbg.trace("DebuggerSession: [Route] %s from %s (remote=%s)" % [msg, pk, is_remote])


# ─── Message Handlers ─────────────────────────────────────────────────────────

func _on_session_registered(envelope: NetEnvelope, is_remote: bool = false) -> void:
	var pk := envelope.peer_key()
	var d := envelope.payload
	var username := d.get("username", "")
	var peer_id := envelope.peer_id
	var is_server: bool = d.get("is_server", false) or peer_id == 1
	var tree_name: String = d.get("tree_name", envelope.source_path.get_file())
	
	# If already online, update identity if it was unknown at registration time.
	if pk in _peers and _peers[pk].get("online", false):
		var changed := false
		
		# Upgrade path: if we were remote but just got a local registration, prefer local.
		if not is_remote and _peers[pk].get("is_remote", false):
			_peers[pk]["is_remote"] = false
			changed = true
			Netw.dbg.info("DebuggerSession: [UpgradeToLocal] peer %s" % [pk])
		
		if _peers[pk].get("peer_id", 0) == 0 and peer_id != 0:
			_peers[pk]["peer_id"] = peer_id
			peer_id_resolved.emit(pk, peer_id)
		
		# Update username if it changed (e.g. from empty to a real name).
		if _peers[pk].get("username", "") != username and not username.is_empty():
			_peers[pk]["username"] = username
			_peers[pk]["display_name"] = _get_display_name(username, _peers[pk]["tree_name"], _peers[pk]["is_server"])
			changed = true
		
		if changed:
			peer_identity_changed.emit(pk, username)
		
		Netw.dbg.debug("DebuggerSession: [IgnoreDuplicate] for peer %s" % [pk])
		return
	
	Netw.dbg.info("DebuggerSession: [Registered] '%s' (peer=%d, remote=%s, path=%s)" % [username if not username.is_empty() else tree_name, peer_id, is_remote, envelope.source_path])
	
	var color: Color = _assign_peer_color(peer_id)
	_peers[pk] = {
		"username": username,
		"tree_name": tree_name,
		"display_name": _get_display_name(username, tree_name, is_server),
		"is_server": is_server,
		"backend_class": d.get("backend_class", ""),
		"online": true,
		"color": color,
		"peer_id": peer_id,
		"is_remote": is_remote,
	}
	
	peer_registered.emit(
		pk, 
		username, 
		tree_name, 
		is_server, 
		color, 
		is_remote, 
		peer_id
	)

	for pt in PanelDataAdapter.PANEL_NAMES.keys():
		if is_server:
			if pt == PanelDataAdapter.PanelType.TOPOLOGY:
				continue

		var key: String = _adapter_key(pk, pt)
		if key not in _adapters:
			var display_name := username if not username.is_empty() else tree_name
			_adapters[key] = _create_adapter(pk, display_name, pt)
			_adapters[key].data_changed.connect(
				func(k: String) -> void: adapter_data_changed.emit(k)
			)

	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [auto_break])


func _on_session_unregistered(envelope: NetEnvelope, is_remote: bool = false) -> void:
	var pk := envelope.peer_key()
	if pk not in _peers: return
	_peers[pk]["online"] = false
	peer_status_changed.emit(pk, false)
	
	if is_remote and plugin:
		plugin.send_to_game(session_id, "networked:remote_session_unregistered",
				[envelope.to_dict()])


func _on_peer_event(envelope: NetEnvelope, connected: bool) -> void:
	var pk := envelope.peer_key()
	if pk not in _peers: return
	if connected:
		_peers[pk]["peer_id"] = envelope.peer_id

	for pt in PanelDataAdapter.PANEL_NAMES.keys():
		var key := _adapter_key(pk, pt)
		if key in _adapters:
			_adapters[key].on_peer_event(envelope.payload, connected)


func _on_clock_sample(envelope: NetEnvelope, is_remote: bool = false) -> void:
	if is_remote and plugin:
		# Forward the remote clock sample back to the game process associated
		# with this session. This allows the game process to register a
		# Performance monitor for the remote peer.
		plugin.send_to_game(session_id, "networked:remote_clock_sample",
				[envelope.to_dict()])


func _on_crash_manifest(envelope: NetEnvelope) -> void:
	var pk := envelope.peer_key()
	var payload := envelope.payload
	var trigger: String = payload.get("trigger", "UNKNOWN")
	var cid: String = payload.get("cid", "?")

	Netw.dbg.info("DebuggerSession: [ReceiveManifest] %s (cid=%s) from %s" % [trigger, cid, pk])

	var uid: String = payload.get("uid", "")
	# DEDUP-3 (replay idempotency): crash manifests are replayed to late-joining editors
	# via _emit_current_state(). This prevents a manifest appearing twice when both the
	# live event and the history replay arrive in the same editor session.
	if not uid.is_empty():
		if pk not in _seen_crash_cids:
			_seen_crash_cids[pk] = {}
		if uid in _seen_crash_cids[pk]:
			return
		_seen_crash_cids[pk][uid] = true
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


func _get_display_name(username: String, tree_name: String, is_server: bool) -> String:
	if is_server:
		return tree_name
	
	if username.is_empty():
		return tree_name
		
	return "%s [%s]" % [tree_name, username]
