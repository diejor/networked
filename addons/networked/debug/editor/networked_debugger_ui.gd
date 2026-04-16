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
var _span_history: Array[Dictionary] = []

# Panels.
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

	_tree_selector = OptionButton.new()
	_tree_selector.custom_minimum_size.x = 180
	_tree_selector.tooltip_text = "Select active MultiplayerTree"
	_tree_selector.item_selected.connect(_on_tree_selected)
	bar.add_child(_tree_selector)

	bar.add_child(VSeparator.new())

	_lamp = ColorRect.new()
	_lamp.custom_minimum_size = Vector2(12, 12)
	_lamp.color = Color.GRAY
	_lamp.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	bar.add_child(_lamp)

	_status_label = Label.new()
	_status_label.text = "offline"
	bar.add_child(_status_label)

	bar.add_child(VSeparator.new())

	_freeze_btn = CheckButton.new()
	_freeze_btn.text = "Freeze"
	_freeze_btn.toggled.connect(func(v: bool) -> void: _freeze = v)
	bar.add_child(_freeze_btn)

	var break_btn := CheckButton.new()
	break_btn.text = "Break on Manifest"
	break_btn.tooltip_text = "Pause the game the moment a crash manifest is generated."
	break_btn.toggled.connect(_on_auto_break_toggled)
	bar.add_child(break_btn)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(spacer)

	var export_btn := Button.new()
	export_btn.text = "Export"
	export_btn.tooltip_text = "Export current debugger state to clipboard."
	export_btn.pressed.connect(_on_export_state)
	bar.add_child(export_btn)


func _build_tabs() -> void:
	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(tabs)

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

	_panel_log.toggle_breakpoint = func(source: String, line: int) -> void:
		var script = load(source)
		if not script is Script:
			return
		# Determine new state from our own tracking — avoids needing CodeEdit
		# before the editor has switched screens.
		var new_state: bool = not bool(_panel_log._active_breakpoints.get(
				"%s:%d" % [source, line], false))
		# Update the panel icon immediately.
		_panel_log.sync_breakpoint(source, line, new_state)
		# Navigate to the source.
		EditorInterface.set_main_screen_editor("Script")
		EditorInterface.edit_script(script, line)
		# Defer the CodeEdit write: set_main_screen_editor is asynchronous, so
		# get_current_editor() returns null if called in the same frame.
		(func() -> void:
			var se := EditorInterface.get_script_editor()
			if not se:
				return
			var ed := se.get_current_editor()
			if not ed:
				return
			var ce := ed.get_base_editor() as CodeEdit
			if ce:
				ce.set_line_as_breakpoint(line - 1, new_state)
		).call_deferred()


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
		"networked:crash_manifest":       _on_crash_manifest(d)
		"networked:span_open":
			_span_history.append({"type": "open", "data": d})
			if _matches_selected(d.get("tree_name", "")):
				_panel_log.push_span_open(d)
		"networked:span_step":
			_span_history.append({"type": "step", "data": d})
			_panel_log.push_span_step(d)
		"networked:span_close":
			_span_history.append({"type": "close", "data": d})
			_panel_log.push_span_close(d)
		"networked:span_fail":
			_span_history.append({"type": "fail", "data": d})
			_panel_log.push_span_fail(d)


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


func on_breakpoint_changed(source: String, line: int, enabled: bool) -> void:
	_panel_log.sync_breakpoint(source, line, enabled)


func on_breakpoints_cleared() -> void:
	_panel_log.sync_breakpoints_cleared()


func _on_manifest_context_selected(ctx: Dictionary) -> void:
	var cid: String = ctx.get("cid", "")

	if not cid.is_empty():
		_panel_log.highlight_cid(cid)


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
	_panel_log.clear()

	for entry: Dictionary in _span_history:
		var d: Dictionary = entry.data
		match entry.type:
			"open":
				if _matches_selected(d.get("tree_name", "")):
					_panel_log.push_span_open(d)
			"step":  _panel_log.push_span_step(d)
			"close": _panel_log.push_span_close(d)
			"fail":  _panel_log.push_span_fail(d)

	if _selected_tree in _clock_samples:
		for s in _clock_samples[_selected_tree]:
			_panel_clock.push_sample(s)


func _matches_selected(tree_name: String) -> bool:
	return tree_name.is_empty() or _selected_tree.is_empty() or tree_name == _selected_tree


# ─── Export State ─────────────────────────────────────────────────────────────

func reset_session() -> void:
	_trees.clear()
	_clock_samples.clear()
	_span_history.clear()
	_selected_tree = ""
	_tree_selector.clear()
	_lamp.color = Color.GRAY
	_status_label.text = "offline"
	_panel_clock.clear()
	_panel_log.clear()
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

	return {
		"selected_tree": _selected_tree,
		"trees":         _trees,
		"clock":         clock_out,
	}


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
