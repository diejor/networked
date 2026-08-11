## Right pane of the Networked debugger: a scrollable grid of [PanelWrapper]s,
## one per active [code](peer, panel)[/code] pair. Column count auto-fits to
## [code]ceil(sqrt(active))[/code]; double-clicking a wrapper title maximizes it.
##
## Owns panel lifecycle (activate / deactivate / populate), the panel factory,
## and breakpoint fan-out. Reads adapters and routes game commands through the
## injected [DebuggerSession]. Reports status-icon clicks up through
## [signal status_detail_requested] so [NetworkedDebuggerUI] shows the dialog.
@tool
class_name PanelGrid
extends ScrollContainer

const PanelDataAdapter := preload("res://addons/networked/debug/editor/adapters/panel_data.gd")

## Emitted when a wrapper's header status icon is clicked.
signal status_detail_requested(title: String, level: int, summary: String)

## Injected by [NetworkedDebuggerUI] before this node enters the scene tree.
var session: DebuggerSession

var _grid: GridContainer

# [Dictionary] mapping [code]adapter_key[/code] to its [PanelWrapper].
var _panel_wrappers: Dictionary[String, PanelWrapper] = { }

# Ordered list of active adapter keys (controls grid child order).
var _active_keys: Array[String] = []

# When non-empty, only this key's wrapper is shown (maximized).
var _maximized_key: String = ""

# Keys awaiting initial populate after entering the scene tree.
var _pending_populate: Dictionary = { }

var _dbg: NetwHandle = Netw.dbg.handle(self)
var _is_applying_theme: bool = false


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	_grid = GridContainer.new()
	_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_grid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_grid.columns = 1
	_grid.add_theme_constant_override("h_separation", 6)
	_grid.add_theme_constant_override("v_separation", 6)

	# Faint backdrop so the grid boundary is visible.
	var grid_bg := StyleBoxFlat.new()
	grid_bg.bg_color = Color(0, 0, 0, 0.1)
	_grid.add_theme_stylebox_override("panel", grid_bg)

	add_child(_grid)


func _ready() -> void:
	_apply_theme()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_apply_theme()


# Editor-theme-aware backdrop; re-runs on theme change so light/dark updates live.
func _apply_theme() -> void:
	if not is_inside_tree() or _is_applying_theme:
		return
	_is_applying_theme = true
	# ScrollContainer uses "panel" for its backdrop in the editor theme.
	var scroll_bg := StyleBoxFlat.new()
	scroll_bg.bg_color = get_theme_color("base_color", "Editor")
	scroll_bg.set_corner_radius_all(4)
	scroll_bg.content_margin_left = 4
	scroll_bg.content_margin_right = 4
	scroll_bg.content_margin_top = 4
	scroll_bg.content_margin_bottom = 4
	add_theme_stylebox_override("panel", scroll_bg)
	_is_applying_theme = false

# ─── Public API (driven by the coordinator) ───────────────────────────────────


## Opens or closes the panel for a [code](peer, panel_type)[/code] pair.
func set_panel_active(peer_key: String, panel_type: int, active: bool) -> void:
	var key := "%s:%s" % [peer_key, PanelDataAdapter.PANEL_NAMES[panel_type]]
	if active:
		_activate_panel(key, peer_key, panel_type)
	else:
		_deactivate_panel(key)
	_rebuild_grid()


## Closes every panel belonging to a peer (peer unregistered).
func deactivate_peer(peer_key: String) -> void:
	var prefix := peer_key + ":"
	var to_deactivate: Array[String] = []
	for key in _panel_wrappers:
		if key.begins_with(prefix):
			to_deactivate.append(key)
	for key in to_deactivate:
		_deactivate_panel(key)
	_rebuild_grid()


## Pushes fresh adapter data into an open wrapper (status, live metric, entry,
## cross-panel highlight). No-op if the panel is not open.
func apply_adapter_data(key: String) -> void:
	if key not in _panel_wrappers or not session:
		return
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		return

	var wrapper: PanelWrapper = _panel_wrappers[key]
	wrapper.set_status(adapter.get_status_level(), adapter.get_status_banner_text())

	if adapter.ring_buffer.is_empty():
		return
	wrapper.update_live_metric(adapter.get_current_label())
	wrapper.panel_control.on_new_entry(adapter.ring_buffer[-1])

	# Cross-panel sync: crash arrival highlights the peer's Span Tracer.
	var crash_name := PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.CRASH]
	if key.ends_with(":" + crash_name):
		var last: Dictionary = adapter.ring_buffer[-1] as Dictionary
		var cid: String = last.get("cid", "")
		if not cid.is_empty():
			var span_name := PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]
			var span_key: String = key.replace(crash_name, span_name)
			if span_key in _panel_wrappers:
				var panel := _panel_wrappers[span_key].panel_control
				(panel as PanelSpanTracer).highlight_cid(cid)


## Propagates online state to all of a peer's open wrappers.
func set_peer_online(peer_key: String, online: bool) -> void:
	var prefix := peer_key + ":"
	for key in _panel_wrappers:
		if key.begins_with(prefix):
			_panel_wrappers[key].set_online(online)


## Deactivates all panels (session cleared / restart).
func clear_panels() -> void:
	for key: String in _active_keys.duplicate():
		_deactivate_panel(key)
	_active_keys.clear()
	_panel_wrappers.clear()
	_pending_populate.clear()
	_maximized_key = ""
	_rebuild_grid()


## Called by [NetworkedDebuggerPlugin._breakpoint_set_in_tree].
func on_breakpoint_changed(source: String, line: int, enabled: bool) -> void:
	var span_name := PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]
	for key: String in _active_keys:
		if not key.ends_with(":" + span_name) or key not in _panel_wrappers:
			continue
		var panel := _panel_wrappers[key].panel_control as PanelSpanTracer
		if panel:
			panel.sync_breakpoint(source, line, enabled)


## Called by [NetworkedDebuggerPlugin._breakpoints_cleared_in_tree].
func on_breakpoints_cleared() -> void:
	var span_name := PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]
	for key: String in _active_keys:
		if not key.ends_with(":" + span_name) or key not in _panel_wrappers:
			continue
		var panel := _panel_wrappers[key].panel_control as PanelSpanTracer
		if panel:
			panel.sync_breakpoints_cleared()

# ─── Panel lifecycle ──────────────────────────────────────────────────────────


func _activate_panel(
		key: String,
		peer_key: String,
		pt: PanelDataAdapter.PanelType,
) -> void:
	if key in _panel_wrappers or not session:
		return
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		_dbg.warn(
			"Grid: [ActivateFailed] Adapter not found for key: %s" % [key],
			func(m): push_warning(m),
		)
		return

	_dbg.info("Grid: [ActivatePanel] %s" % [key])
	var peer_info: Dictionary = session.get_peers().get(peer_key, { })
	var color: Color = peer_info.get("color", Color.WHITE)
	var peer_display: String = peer_info.get("display_name", peer_key)

	# If this is a remote peer, sync its history from the local owner session.
	if peer_info.get("is_remote", false) and session.plugin:
		session.plugin.sync_history(
			session.session_id,
			peer_key,
			PanelDataAdapter.PANEL_NAMES[pt],
		)

	var panel := _create_panel_control(pt, peer_key)
	var title_str := "%s - %s" % [peer_display, PanelDataAdapter.PANEL_DISPLAY_NAMES[pt]]

	var wrapper := PanelWrapper.new(key, peer_key, title_str, color, panel)
	wrapper.on_maximize_requested = _on_maximize_requested
	wrapper.status_pressed.connect(
		func(l, s): status_detail_requested.emit(title_str, l, s)
	)

	_panel_wrappers[key] = wrapper
	_active_keys.append(key)

	# Populate is deferred to _add_wrapper_to_grid (after the panel enters tree).
	_pending_populate[key] = null


func _deactivate_panel(key: String) -> void:
	if key not in _panel_wrappers:
		return
	var wrapper: PanelWrapper = _panel_wrappers[key]
	if wrapper.is_inside_tree():
		wrapper.get_parent().remove_child(wrapper)
	wrapper.queue_free()
	_panel_wrappers.erase(key)
	_active_keys.erase(key)
	_pending_populate.erase(key)
	if _maximized_key == key:
		_maximized_key = ""


func _rebuild_grid() -> void:
	for child: Node in _grid.get_children():
		_grid.remove_child(child)

	if not _maximized_key.is_empty() and _maximized_key in _panel_wrappers:
		_grid.columns = 1
		_add_wrapper_to_grid(_panel_wrappers[_maximized_key], _maximized_key)
		return

	_grid.columns = maxi(ceili(sqrt(float(_active_keys.size()))), 1)
	for key: String in _active_keys:
		if key in _panel_wrappers:
			_add_wrapper_to_grid(_panel_wrappers[key], key)
		else:
			_dbg.warn(
				"Grid: [RebuildFailed] Wrapper missing for key: %s" % [key],
				func(m): push_warning(m),
			)


# Adds a wrapper to the grid and populates it if newly activated. [method _ready]
# fires synchronously on add_child, so post-ready calls are safe after.
func _add_wrapper_to_grid(wrapper: PanelWrapper, key: String) -> void:
	_grid.add_child(wrapper) # triggers _ready() on wrapper and its children

	if session:
		var peer_info: Dictionary = session.get_peers().get(wrapper.peer_key, { })
		wrapper.init_peer_context(
			peer_info.get("is_remote", false),
			peer_info.get("online", true),
		)
		if wrapper.panel_control is PanelCrashManifest:
			(wrapper.panel_control as PanelCrashManifest)._break_btn.set_pressed_no_signal(session.auto_break)

	if key in _pending_populate:
		_pending_populate.erase(key)
		if session:
			var adapter: PanelDataAdapter = session.get_adapter(key)
			if adapter:
				wrapper.panel_control.populate(adapter.ring_buffer)
				apply_adapter_data(key) # initialise summary tooltips / metrics


func _on_maximize_requested(key: String) -> void:
	_maximized_key = "" if _maximized_key == key else key
	_rebuild_grid()

# ─── Panel factory ────────────────────────────────────────────────────────────


func _create_panel_control(
		pt: PanelDataAdapter.PanelType,
		peer_key: String,
) -> Control:
	var control: Control
	match pt:
		PanelDataAdapter.PanelType.SPAN:
			control = PanelSpanTracer.new()
			# Inject breakpoint toggle Callable.
			(control as PanelSpanTracer).toggle_breakpoint = func(src: String, ln: int) -> void:
				var script: Script = load(src) as Script
				if not script:
					return
				var act_bp := (control as PanelSpanTracer)._active_breakpoints
				var new_state: bool = not bool(act_bp.get("%s:%d" % [src, ln], false))
				(control as PanelSpanTracer).sync_breakpoint(src, ln, new_state)
				EditorInterface.set_main_screen_editor("Script")
				EditorInterface.edit_script(script, ln)
				(func() -> void:
					var se := EditorInterface.get_script_editor()
					if not se:
						return
					var ed := se.get_current_editor()
					if not ed:
						return
					var ce := ed.get_base_editor() as CodeEdit
					if ce:
						ce.set_line_as_breakpoint(ln - 1, new_state)
				).call_deferred()
		PanelDataAdapter.PanelType.CRASH:
			control = PanelCrashManifest.new()
			var crash_panel := control as PanelCrashManifest
			# Cross-panel context selection: highlight the peer's span tracer.
			crash_panel.on_context_selected = func(ctx: Dictionary) -> void:
				var cid: String = ctx.get("cid", "")
				if cid.is_empty():
					return
				var span_name := PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]
				var span_key: String = "%s:%s" % [peer_key, span_name]
				if span_key in _panel_wrappers:
					var panel := _panel_wrappers[span_key].panel_control
					(panel as PanelSpanTracer).highlight_cid(cid)

			crash_panel.on_auto_break_changed = func(enabled: bool) -> void:
				if session:
					session.set_auto_break(enabled)
		PanelDataAdapter.PanelType.TOPOLOGY:
			control = PanelTopology.new()
			var topology_panel := control as PanelTopology
			topology_panel.on_node_inspect = func(node_path: String, pid: int) -> void:
				if session:
					session.send_node_inspect(peer_key, node_path, pid)

			topology_panel.on_refresh_requested = func() -> void:
				if session and session.plugin:
					session.plugin.send_to_game(
						session.session_id,
						"networked:request_snapshot",
						[],
					)

			topology_panel.on_nameplate_toggled = func(path: String, en: bool, pid: int) -> void:
				if session:
					session.send_visualizer_toggle(peer_key, path, "nameplate", en, pid)

	if control:
		control.size_flags_vertical = Control.SIZE_EXPAND_FILL
		control.custom_minimum_size = Vector2(300, 200)
		return control

	return Control.new()
