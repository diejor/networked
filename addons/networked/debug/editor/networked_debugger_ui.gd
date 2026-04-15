## Main "Networked" tab in the editor debugger.
##
## Hosts the Session Bar and the four sub-panels. All incoming game messages
## are routed here via [method on_message]. Ring buffers retain recent history
## so context is preserved when switching trees in the Session Bar dropdown.
@tool
class_name NetworkedDebuggerUI
extends VBoxContainer

const RING_SIZE := 50

# Set by the plugin after construction.
var plugin: NetworkedDebuggerPlugin
var session_id: int

# Whether the Freeze toggle is active.
var _freeze: bool = false

# Desired auto-break state. Stored here so it can be re-sent when the game
# session becomes active (the toggle may be set before the game starts).
var _auto_break: bool = false

# Known trees: tree_name → {is_server, backend_class, online}
var _trees: Dictionary[String, Dictionary] = {}

# Currently selected tree name ("" means first available).
var _selected_tree: String = ""

# Ring buffers keyed by tree_name.
var _clock_samples: Dictionary[String, Array] = {}
var _lobby_snapshots: Dictionary[String, Dictionary] = {}
var _component_heartbeats: Dictionary[String, Dictionary] = {}
var _component_events: Array = []  # flat ring, last RING_SIZE

# Panels.
var _panel_matrices: PanelMatrices
var _panel_components: PanelComponents
var _panel_log: PanelLogBridge
var _panel_clock: PanelClock
var _panel_crash_manifest: PanelCrashManifest

# NodePath prefix (as String) → alias (e.g. "/root/.../Level1" → "[Lobby:Level1]")
# Populated by lobby_spawned / lobby_despawned events forwarded from the reporter.
var _alias_map: Dictionary = {}

# Session Bar widgets.
var _tree_selector: OptionButton
var _lamp: ColorRect
var _status_label: Label
var _freeze_btn: CheckButton


func _ready() -> void:
	custom_minimum_size.y = 250
	_build_session_bar()
	_build_tabs()


func _build_session_bar() -> void:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 8)
	add_child(bar)

	var lbl := Label.new()
	lbl.text = "Tree:"
	bar.add_child(lbl)

	_tree_selector = OptionButton.new()
	_tree_selector.custom_minimum_size.x = 180
	_tree_selector.item_selected.connect(_on_tree_selected)
	bar.add_child(_tree_selector)

	_lamp = ColorRect.new()
	_lamp.custom_minimum_size = Vector2(12, 12)
	_lamp.color = Color.GRAY
	bar.add_child(_lamp)

	_status_label = Label.new()
	_status_label.text = "offline"
	bar.add_child(_status_label)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(spacer)

	_freeze_btn = CheckButton.new()
	_freeze_btn.text = "Freeze"
	_freeze_btn.toggled.connect(func(v: bool) -> void: _freeze = v)
	bar.add_child(_freeze_btn)

	var break_btn := CheckButton.new()
	break_btn.text = "Break on Manifest"
	break_btn.tooltip_text = "Pause the game (like a breakpoint) the moment a crash manifest is generated."
	break_btn.toggled.connect(_on_auto_break_toggled)
	bar.add_child(break_btn)

	var export_btn := Button.new()
	export_btn.text = "Export State"
	export_btn.pressed.connect(_on_export_state)
	bar.add_child(export_btn)


func _build_tabs() -> void:
	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(tabs)

	_panel_matrices = PanelMatrices.new()
	_panel_matrices.name = "Matrices"
	_panel_matrices.send_to_game = func(msg: String, data: Array) -> void:
		if plugin:
			plugin.send_to_game(session_id, msg, data)
	tabs.add_child(_panel_matrices)

	_panel_components = PanelComponents.new()
	_panel_components.name = "Components"
	tabs.add_child(_panel_components)

	_panel_log = PanelLogBridge.new()
	_panel_log.name = "Log Bridge"
	tabs.add_child(_panel_log)

	_panel_clock = PanelClock.new()
	_panel_clock.name = "Clock"
	tabs.add_child(_panel_clock)

	_panel_crash_manifest = PanelCrashManifest.new()
	_panel_crash_manifest.name = "Crash Manifest"
	_panel_crash_manifest.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_panel_crash_manifest.on_context_selected = _on_manifest_context_selected
	tabs.add_child(_panel_crash_manifest)


# ─── Message Dispatch ─────────────────────────────────────────────────────────

func on_message(message: String, data: Array) -> void:
	if _freeze or data.is_empty():
		return
	var d: Dictionary = data[0] if data[0] is Dictionary else {}

	match message:
		"networked:session_registered":   _on_session_registered(d)
		"networked:session_unregistered": _on_session_unregistered(d)
		"networked:peer_connected":       _on_peer_event(d, true)
		"networked:peer_disconnected":    _on_peer_event(d, false)
		"networked:clock_sample":         _on_clock_sample(d)
		"networked:lobby_snapshot":       _on_lobby_snapshot(d)
		"networked:lobby_event":          _on_lobby_event(d)
		"networked:component_heartbeat":  _on_component_heartbeat(d)
		"networked:component_event":      _on_component_event(d)
		"networked:replication_snapshot": _on_replication_snapshot(d)
		"networked:crash_manifest":       _on_crash_manifest(d)
		"networked:span_open":            _panel_log.push_span_open(d)
		"networked:span_step":            _panel_log.push_span_step(d)
		"networked:span_close":           _panel_log.push_span_close(d)
		"networked:span_fail":            _panel_log.push_span_fail(d)
		"networked:span_peer_tagged":     pass  # handled implicitly by span_open peers list
		"networked:span_registered":      pass  # reserved for future watch-toggle UI


func _on_session_registered(d: Dictionary) -> void:
	var name: String = d.get("tree_name", "")
	if name.is_empty():
		return
	_trees[name] = {
		"is_server": d.get("is_server", false),
		"backend_class": d.get("backend_class", ""),
		"online": true,
	}
	# Game just connected — push current toggle state so it takes effect
	# even if the button was set before the game started.
	_send_auto_break_state()
	_refresh_tree_selector()


func _on_session_unregistered(d: Dictionary) -> void:
	var name: String = d.get("tree_name", "")
	if name in _trees:
		_trees[name]["online"] = false
	_refresh_tree_selector()


func _on_peer_event(d: Dictionary, _connected: bool) -> void:
	_update_status(d.get("tree_name", ""))


func _on_clock_sample(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if not tn in _clock_samples:
		_clock_samples[tn] = []
	var buf: Array = _clock_samples[tn]
	buf.append(d)
	if buf.size() > RING_SIZE:
		buf.pop_front()
	if _matches_selected(tn):
		_panel_clock.push_sample(d)


func _on_lobby_snapshot(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	_lobby_snapshots[tn] = d
	if _matches_selected(tn):
		_panel_matrices.update_visibility_matrix(d)


func _on_lobby_event(_d: Dictionary) -> void:
	pass  # Snapshots are polled at 2 Hz; events only invalidate cache if needed.


func _on_component_heartbeat(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if not tn in _component_heartbeats:
		_component_heartbeats[tn] = {}
	_component_heartbeats[tn][d.get("player_name", "")] = d
	if _matches_selected(tn):
		_panel_components.update_player(d)


func _on_component_event(d: Dictionary) -> void:
	_component_events.append(d)
	if _component_events.size() > RING_SIZE:
		_component_events.pop_front()
	if _matches_selected(d.get("tree_name", "")):
		_panel_log.push_event(d)


func _on_replication_snapshot(d: Dictionary) -> void:
	if _matches_selected(d.get("tree_name", "")):
		_panel_matrices.update_replication_matrix(d)


func _on_crash_manifest(d: Dictionary) -> void:
	if not is_instance_valid(_panel_crash_manifest):
		return
	var entry := ManifestFormatter.format(d, _alias_map)
	_panel_crash_manifest.push_entry(entry)


# ─── Orchestrator Bus ─────────────────────────────────────────────────────────

func _on_auto_break_toggled(enabled: bool) -> void:
	_auto_break = enabled
	_send_auto_break_state()


func _send_auto_break_state() -> void:
	if plugin:
		plugin.send_to_game(session_id, "networked:set_auto_break", [_auto_break])


func _on_manifest_context_selected(ctx: Dictionary) -> void:
	var cid: String = ctx.get("cid", "")
	var player: String = ctx.get("player_name", "")

	if not cid.is_empty():
		_panel_log.highlight_cid(cid)
	if not player.is_empty():
		_panel_components.highlight_player(player)
	# Lobby name can be derived from the selected tree's latest snapshot.
	var tn: String = ctx.get("tree_name", _selected_tree)
	if tn in _lobby_snapshots:
		var lobbies: Array = _lobby_snapshots[tn].get("lobbies", [])
		if not lobbies.is_empty():
			_panel_matrices.highlight_lobby(lobbies[0].get("name", ""))


# ─── Session Bar Logic ────────────────────────────────────────────────────────

func _refresh_tree_selector() -> void:
	var prev := _tree_selector.get_item_text(_tree_selector.selected) if _tree_selector.selected >= 0 else ""
	_tree_selector.clear()
	var idx := 0
	var restore_idx := 0
	for tn: String in _trees:
		var info: Dictionary = _trees[tn]
		var prefix := "[S] " if info.get("is_server", false) else "[C] "
		var suffix := "" if info.get("online", false) else " (offline)"
		_tree_selector.add_item(prefix + tn + suffix)
		if tn == prev:
			restore_idx = idx
		idx += 1
	if _tree_selector.item_count > 0:
		_tree_selector.select(restore_idx)
		_on_tree_selected(restore_idx)


func _on_tree_selected(idx: int) -> void:
	if idx < 0 or idx >= _tree_selector.item_count:
		return
	# Extract tree name from label (strip prefix and suffix).
	var raw := _tree_selector.get_item_text(idx)
	_selected_tree = raw.substr(4).split(" (")[0]  # strip "[S] " or "[C] " and " (offline)"
	_update_status(_selected_tree)
	_repopulate_panels()


func _update_status(tree_name: String) -> void:
	if tree_name != _selected_tree:
		return
	var info: Dictionary = _trees.get(tree_name, {})
	var online: bool = info.get("online", false)
	_lamp.color = Color.GREEN if online else Color.RED
	_status_label.text = "online" if online else "offline"


func _repopulate_panels() -> void:
	_panel_clock.clear()
	_panel_components.clear()
	_panel_log.clear()
	_panel_matrices.clear()

	if _selected_tree in _clock_samples:
		for s in _clock_samples[_selected_tree]:
			_panel_clock.push_sample(s)

	if _selected_tree in _lobby_snapshots:
		_panel_matrices.update_visibility_matrix(_lobby_snapshots[_selected_tree])

	if _selected_tree in _component_heartbeats:
		for d in _component_heartbeats[_selected_tree].values():
			_panel_components.update_player(d)

	for ev in _component_events:
		if ev.get("tree_name", "") == _selected_tree:
			_panel_log.push_event(ev)


func _matches_selected(tree_name: String) -> bool:
	return _selected_tree.is_empty() or tree_name == _selected_tree


# ─── Export State ─────────────────────────────────────────────────────────────

func reset_session() -> void:
	_trees.clear()
	_clock_samples.clear()
	_lobby_snapshots.clear()
	_component_heartbeats.clear()
	_component_events.clear()
	_selected_tree = ""
	_tree_selector.clear()
	_lamp.color = Color.GRAY
	_status_label.text = "offline"
	_panel_clock.clear()
	_panel_components.clear()
	_panel_log.clear()
	_panel_matrices.clear()
	if is_instance_valid(_panel_crash_manifest):
		_panel_crash_manifest.clear()
	_alias_map.clear()


func _on_export_state() -> void:
	DisplayServer.clipboard_set(JSON.stringify(_build_export_summary(), "\t"))
	print("[Networked Debugger] State exported to clipboard.")


func _build_export_summary() -> Dictionary:
	# ── Clock ──────────────────────────────────────────────────────────────────
	var clock_out: Dictionary = {}
	for tn: String in _clock_samples:
		var samples: Array = _clock_samples[tn]
		if samples.is_empty():
			continue
		clock_out[tn] = {
			"n":          samples.size(),
			"rtt_ms":     _stats_ms(samples, "rtt_avg"),
			"jitter_ms":  _stats_ms(samples, "rtt_jitter"),
			"diff_ticks": _stats_int(samples, "diff"),
			"last":       samples[-1],
		}

	# ── Components: cross-player baseline + per-player diffs ──────────────────
	var comp_out: Dictionary = {}
	for tn: String in _component_heartbeats:
		var players: Dictionary = _component_heartbeats[tn]
		if players.is_empty():
			continue
		var defaults: Dictionary = _compute_component_defaults(players.values())
		var diffs: Dictionary = {}
		for pname: String in players:
			diffs[pname] = _diff_components(players[pname].get("components", {}), defaults)
		comp_out[tn] = {"_defaults": defaults, "players": diffs}

	# ── Events: group by player; ops listed, standalones counted ─────────────
	var by_player: Dictionary = {}  # player → {ops: {cid→dict}, events: {key→dict}}
	for ev: Dictionary in _component_events:
		var player: String = ev.get("player_name", "")
		var cid: String    = ev.get("correlation_id", "")
		var etype: String  = ev.get("event_type", "?")
		var tree: String   = ev.get("tree_name", "?")
		var ts: int        = ev.get("timestamp_usec", 0)

		if not player in by_player:
			by_player[player] = {"ops": {}, "events": {}}

		if cid.is_empty():
			# Collapse flapping: count by (tree, type), not one entry per occurrence.
			var key: String = tree + ":" + etype
			if not key in by_player[player]["events"]:
				by_player[player]["events"][key] = {"type": etype, "tree": tree, "count": 0}
			by_player[player]["events"][key]["count"] += 1
		else:
			var pops: Dictionary = by_player[player]["ops"]
			if not cid in pops:
				pops[cid] = {
					"cid":        cid.substr(0, 12),
					"steps":      [],
					"tree":       tree,
					"start_usec": ts,
					"duration_ms": 0.0,
				}
			pops[cid]["steps"].append(etype)
			pops[cid]["duration_ms"] = (ts - pops[cid]["start_usec"]) / 1000.0

	var events_out: Dictionary = {}
	for player: String in by_player:
		var pd: Dictionary = by_player[player]
		var op_list: Array = pd["ops"].values()
		if op_list.size() > 10:
			op_list = op_list.slice(op_list.size() - 10)
		var ev_list: Array = pd["events"].values()
		var entry: Dictionary = {}
		if not op_list.is_empty():
			entry["ops"] = op_list
		if not ev_list.is_empty():
			entry["events"] = ev_list
		if not entry.is_empty():
			events_out[player] = entry

	return {
		"selected_tree": _selected_tree,
		"trees":         _trees,
		"clock":         clock_out,
		"lobbies":       _lobby_snapshots,
		"components":    comp_out,
		"events":        events_out,
	}


## Finds fields that are identical across ALL players for each component type.
## These become the "default template" so per-player diffs only show deviations.
func _compute_component_defaults(player_list: Array) -> Dictionary:
	if player_list.is_empty():
		return {}
	var first: Dictionary = (player_list[0] as Dictionary).get("components", {})
	var defaults: Dictionary = {}
	for comp_type: String in first:
		var first_comp: Dictionary = first[comp_type] if first[comp_type] is Dictionary else {}
		var comp_defaults: Dictionary = {}
		for field in first_comp:
			var val: Variant = first_comp[field]
			var all_same := true
			for pd: Dictionary in player_list:
				if pd.get("components", {}).get(comp_type, {}).get(field) != val:
					all_same = false
					break
			if all_same:
				comp_defaults[field] = val
		if not comp_defaults.is_empty():
			defaults[comp_type] = comp_defaults
	return defaults


## Returns only the fields in [param components] that differ from [param defaults].
func _diff_components(components: Dictionary, defaults: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	for comp_type: String in components:
		var comp: Dictionary = components[comp_type] if components[comp_type] is Dictionary else {}
		var def_comp: Dictionary = defaults.get(comp_type, {})
		var diff: Dictionary = {}
		for field in comp:
			if not field in def_comp or comp[field] != def_comp[field]:
				diff[field] = comp[field]
		if not diff.is_empty():
			result[comp_type] = diff
	return result


func _stats_ms(samples: Array, key: String) -> Dictionary:
	var mn := INF; var mx := -INF; var total := 0.0
	for s: Dictionary in samples:
		var v: float = s.get(key, 0.0) * 1000.0
		if v < mn: mn = v
		if v > mx: mx = v
		total += v
	return {
		"min": snappedf(mn if mn != INF else 0.0, 0.01),
		"max": snappedf(mx if mx != -INF else 0.0, 0.01),
		"avg": snappedf(total / samples.size(), 0.01),
	}


func _stats_int(samples: Array, key: String) -> Dictionary:
	var mn := 999999; var mx := -999999; var total := 0
	for s: Dictionary in samples:
		var v: int = int(s.get(key, 0))
		if v < mn: mn = v
		if v > mx: mx = v
		total += v
	return {"min": mn, "max": mx, "avg": total / samples.size()}
