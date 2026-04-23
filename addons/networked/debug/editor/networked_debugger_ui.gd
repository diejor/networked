## Main "Networked" tab in the editor debugger.
##
## A [DebuggerSession] injected by [NetworkedDebuggerPlugin] owns all data.
## This class reacts to session signals and drives the layout.
##
## Checking a child creates a [PanelWrapper] in the right grid and calls
## [method populate] on the panel immediately with existing buffered data.
##
## Right grid: column count auto-adjusts to [code]ceil(sqrt(active_count))[/code].
## Double-clicking a PanelWrapper title bar maximizes it; double-clicking again
## restores.
@tool
class_name NetworkedDebuggerUI
extends VBoxContainer

## Injected by [NetworkedDebuggerPlugin] before the node enters the scene tree.
var session

# --- Layout nodes -------------------------------------------------------------
var _peer_tree: Tree
var _grid: GridContainer
var _scroll: ScrollContainer
var _split: HSplitContainer

# --- State --------------------------------------------------------------------
# peer_key -> bold TreeItem (non-selectable peer header row)
var _peer_tree_items: Dictionary[String, TreeItem] = {}

# adapter_key -> PanelWrapper node currently in the grid
var _panel_wrappers: Dictionary[String, PanelWrapper] = {}

# adapter_key -> TreeItem (checkbox row)
var _checkbox_items: Dictionary[String, TreeItem] = {}

# Ordered list of currently active adapter keys (controls grid child order).
var _active_keys: Array[String] = []

# When non-empty, only this key's wrapper is shown (maximized).
var _maximized_key: String = ""

# Keys awaiting initial populate after entering the scene tree.
# Populated by _activate_panel(); consumed by _rebuild_grid() after add_child().
var _pending_populate: Dictionary = {}

# Status dot icons: pre-rendered tiny circles.
var _dot_online: ImageTexture
var _dot_offline: ImageTexture
var _dot_unknown: ImageTexture


func _ready() -> void:
	custom_minimum_size.y = 250
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	_dot_online  = _make_dot_texture(Color.GREEN)
	_dot_offline = _make_dot_texture(Color.RED)
	_dot_unknown = _make_dot_texture(Color(0.5, 0.5, 0.5))
	_build_layout()
	_apply_theme_styles()
	
	if not session:
		return
	
	session.peer_registered.connect(_on_peer_registered)
	session.peer_status_changed.connect(_on_peer_status_changed)
	session.peer_id_resolved.connect(_on_peer_id_resolved)
	session.adapter_data_changed.connect(_on_adapter_data_changed)
	session.session_cleared.connect(_on_session_cleared)


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_apply_theme_styles()


var _is_applying_theme: bool = false
## Applies editor-theme-aware styles to the tree panel and scroll area.
## Re-runs on NOTIFICATION_THEME_CHANGED so light/dark switches update live.
func _apply_theme_styles() -> void:
	if not is_inside_tree() or not _peer_tree or _is_applying_theme:
		return
	
	_is_applying_theme = true
	# Left tree: slightly darker than the base background, with rounded corners.
	var tree_bg := StyleBoxFlat.new()
	tree_bg.bg_color = get_theme_color("dark_color_2", "Editor")
	tree_bg.set_corner_radius_all(4)
	tree_bg.content_margin_left = 4
	tree_bg.content_margin_right = 4
	tree_bg.content_margin_top = 4
	tree_bg.content_margin_bottom = 4
	_peer_tree.add_theme_stylebox_override("panel", tree_bg)
	
	# Scroll area background: slightly lighter than the tree, rounding matches.
	var scroll_bg := StyleBoxFlat.new()
	scroll_bg.bg_color = get_theme_color("base_color", "Editor")
	scroll_bg.set_corner_radius_all(4)
	scroll_bg.content_margin_left = 4
	scroll_bg.content_margin_right = 4
	scroll_bg.content_margin_top = 4
	scroll_bg.content_margin_bottom = 4
	# ScrollContainer uses "panel" for its backdrop in the editor theme.
	_scroll.add_theme_stylebox_override("panel", scroll_bg)
	_is_applying_theme = false


# ─── Layout construction ──────────────────────────────────────────────────────

func _build_layout() -> void:
	_split = HSplitContainer.new()
	_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_split.split_offset = 260
	add_child(_split)
	
	_peer_tree = Tree.new()
	_peer_tree.custom_minimum_size.x = 260
	_peer_tree.hide_root = true
	_peer_tree.columns = 1
	_peer_tree.set_column_title(0, "Peer")
	_peer_tree.column_titles_visible = true
	_peer_tree.set_column_expand(0, true)
	_peer_tree.item_edited.connect(_on_peer_tree_item_edited)
	_peer_tree.button_clicked.connect(_on_peer_tree_button_clicked)
	# Ensure root exists.
	_peer_tree.create_item()
	_split.add_child(_peer_tree)
	
	_scroll = ScrollContainer.new()
	_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_split.add_child(_scroll)
	
	_grid = GridContainer.new()
	_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_grid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_grid.columns = 1
	_grid.add_theme_constant_override("h_separation", 6)
	_grid.add_theme_constant_override("v_separation", 6)
	
	# Add a background to the grid so we can see its boundaries.
	var grid_bg := StyleBoxFlat.new()
	grid_bg.bg_color = Color(0, 0, 0, 0.1)
	_grid.add_theme_stylebox_override("panel", grid_bg)
	
	_scroll.add_child(_grid)


# ─── Session signal handlers ──────────────────────────────────────────────────

func _on_peer_registered(
	peer_key: String,
	display_name: String,
	is_server: bool,
	color: Color,
	is_remote: bool,
	peer_id: int
) -> void:
	if not _peer_tree.get_root():
		_peer_tree.create_item()
	
	var peer_item := _peer_tree.create_item(_peer_tree.get_root())
	var prefix: String = "[S] " if is_server else "[C] "
	var badge: String = " [remote]" if is_remote else " [local]"
	peer_item.set_text(0, prefix + display_name + badge)
	peer_item.set_custom_color(0, color)
	peer_item.set_icon(0, _dot_online)
	peer_item.set_selectable(0, false)
	
	if peer_id != 0:
		peer_item.set_tooltip_text(0, "peer=%d" % peer_id)
	else:
		peer_item.set_tooltip_text(0, "peer=?")
	
	var font: Font = (
		peer_item.get_tree().get_theme_font(&"bold", &"Tree")
		if peer_item.get_tree() else null
	)
	if font:
		peer_item.set_custom_font(0, font)
	
	_peer_tree_items[peer_key] = peer_item
	
	_add_peer_panel_rows(peer_item, peer_key, is_server)
	
	peer_item.set_collapsed(false)


## Adds panel rows (checkboxes) under [param peer_item].
func _add_peer_panel_rows(peer_item: TreeItem, peer_key: String, is_server: bool) -> void:
	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.TOPOLOGY,
		PanelDataAdapter.PanelType.CRASH,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CLOCK,
	]:
		if pt == PanelDataAdapter.PanelType.TOPOLOGY and is_server:
			continue
		_add_panel_checkbox(peer_item, peer_key, pt)


func _on_peer_status_changed(peer_key: String, online: bool) -> void:
	if peer_key not in _peer_tree_items:
		return
	var peer_item: TreeItem = _peer_tree_items[peer_key]
	peer_item.set_icon(0, _dot_online if online else _dot_offline)
	
	var child := peer_item.get_first_child()
	while child:
		if not online:
			child.set_custom_color(0, Color(0.5, 0.5, 0.5))
		else:
			child.clear_custom_color(0)
		child = child.get_next()
	
	# Propagate online state to all open panels for this peer.
	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CRASH,
		PanelDataAdapter.PanelType.TOPOLOGY,
	]:
		var key := "%s:%s" % [peer_key, PanelDataAdapter.PANEL_NAMES[pt]]
		if key in _panel_wrappers:
			_panel_wrappers[key].set_online(online)


func _on_peer_id_resolved(peer_key: String, peer_id: int) -> void:
	if peer_key not in _peer_tree_items:
		return
	_peer_tree_items[peer_key].set_tooltip_text(0, "peer=%d" % peer_id)


func _on_adapter_data_changed(key: String) -> void:
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		return
	
	var summary := adapter.get_status_banner_text()
	var level := adapter.get_status_level()
	
	# Update Tree tooltip and icon.
	if key in _checkbox_items:
		var item: TreeItem = _checkbox_items[key]
		if not summary.is_empty():
			item.set_tooltip_text(0, summary)
			
			var button_idx := -1
			# Status button is always the first button if it exists.
			if item.get_button_count(0) > 0:
				button_idx = 0
			
			if level > 0:
				var icon_name := "NodeWarning" if level == 1 else "StatusError"
				var color_name := "warning_color" if level == 1 else "error_color"
				var icon := get_theme_icon(icon_name, "EditorIcons")
				var color := get_theme_color(color_name, "Editor")
				var type_str := "Warning" if level == 1 else "Error"
				var panel_name: String = PanelDataAdapter.PANEL_DISPLAY_NAMES[
					adapter.panel_type
				]
				
				if button_idx == -1:
					item.add_button(0, icon)
					button_idx = 0
				else:
					item.set_button(0, button_idx, icon)
				
				item.set_button_color(0, button_idx, color)
				var tip := "%s in %s:\n%s" % [type_str, panel_name, summary]
				item.set_button_tooltip_text(0, button_idx, tip)
			else:
				if button_idx != -1:
					item.erase_button(0, button_idx)
	
	if key not in _panel_wrappers:
		return
	
	var wrapper: PanelWrapper = _panel_wrappers[key]
	wrapper.set_status(level, summary)
	
	if adapter.ring_buffer.is_empty():
		return
	
	wrapper.update_live_metric(adapter.get_current_label())
	wrapper.panel_control.on_new_entry(adapter.ring_buffer[-1])
	
	# Cross-panel sync: crash arrival highlights the peer's Span Tracer.
	var crash_name := PanelDataAdapter.PANEL_NAMES[
		PanelDataAdapter.PanelType.CRASH
	]
	if key.ends_with(":" + crash_name):
		var last: Dictionary = adapter.ring_buffer[-1] as Dictionary
		var cid: String = last.get("cid", "")
		if not cid.is_empty():
			var span_name := PanelDataAdapter.PANEL_NAMES[
				PanelDataAdapter.PanelType.SPAN
			]
			var span_key: String = key.replace(crash_name, span_name)
			if span_key in _panel_wrappers:
				var panel := _panel_wrappers[span_key].panel_control
				(panel as PanelSpanTracer).highlight_cid(cid)


func _on_session_cleared() -> void:
	# Deactivate all panels and remove tree items.
	for key: String in _active_keys.duplicate():
		_deactivate_panel(key)
	_active_keys.clear()
	_panel_wrappers.clear()
	_checkbox_items.clear()
	_peer_tree_items.clear()
	_peer_tree.clear()
	_peer_tree.create_item()  # re-create invisible root
	_maximized_key = ""
	_rebuild_grid()


## Creates a single checkbox row under [param peer_item] for [param pt].
func _add_panel_checkbox(
	peer_item: TreeItem,
	peer_key: String,
	pt: PanelDataAdapter.PanelType
) -> void:
	var child := _peer_tree.create_item(peer_item)
	child.set_cell_mode(0, TreeItem.CELL_MODE_CHECK)
	child.set_text(0, PanelDataAdapter.PANEL_DISPLAY_NAMES[pt])
	child.set_editable(0, true)
	child.set_checked(0, false)
	child.set_metadata(0, {"peer_key": peer_key, "panel_type": pt})
	
	var key: String = "%s:%s" % [peer_key, PanelDataAdapter.PANEL_NAMES[pt]]
	_checkbox_items[key] = child


# ─── Checkbox toggle ──────────────────────────────────────────────────────────

func _on_peer_tree_item_edited() -> void:
	var item: TreeItem = _peer_tree.get_edited()
	if not item:
		return
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var m: Dictionary = meta as Dictionary
	var pk: String = m.get("peer_key", "")
	var pt: PanelDataAdapter.PanelType = m.get("panel_type", 0)
	var key: String = "%s:%s" % [pk, PanelDataAdapter.PANEL_NAMES[pt]]
	var checked: bool = item.is_checked(0)
	
	if checked:
		_activate_panel(key, pk, pt)
	else:
		_deactivate_panel(key)
	_rebuild_grid()


func _on_peer_tree_button_clicked(
	item: TreeItem,
	_column: int,
	_id: int,
	_mb_idx: int
) -> void:
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var m: Dictionary = meta as Dictionary
	var pk: String = m.get("peer_key", "")
	var pt: PanelDataAdapter.PanelType = m.get("panel_type", 0)
	var key: String = "%s:%s" % [pk, PanelDataAdapter.PANEL_NAMES[pt]]
	
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if adapter:
		var level: int = adapter.get_status_level()
		var summary: String = adapter.get_status_banner_text()
		if level > 0:
			var panel_display: String = PanelDataAdapter.PANEL_DISPLAY_NAMES[pt]
			var peers: Dictionary = session.get_peers()
			var peer_info: Dictionary = peers.get(pk, {})
			var peer_display: String = peer_info.get("display_name", pk)
			var title_str := "%s · %s" % [peer_display, panel_display]
			_show_status_detail(title_str, level, summary)


func _show_status_detail(
	panel_name: String,
	level: int,
	summary: String
) -> void:
	var dialog := AcceptDialog.new()
	var type_str := "Warning" if level == 1 else "Error"
	dialog.title = "%s has a %s!" % [panel_name, type_str]
	
	var rtl := RichTextLabel.new()
	rtl.selection_enabled = true
	rtl.bbcode_enabled = true
	rtl.fit_content = true
	rtl.custom_minimum_size = Vector2(400, 100)
	
	var bullet_text := ""
	var lines := summary.split("|")
	for line in lines:
		bullet_text += "[color=gray]•[/color] " + line.strip_edges() + "\n"
	
	rtl.text = bullet_text
	dialog.add_child(rtl)
	add_child(dialog)
	dialog.popup_centered()
	dialog.visibility_changed.connect(
		func(): if not dialog.visible: dialog.queue_free()
	)


func _activate_panel(
	key: String,
	peer_key: String,
	pt: PanelDataAdapter.PanelType
) -> void:
	if key in _panel_wrappers:
		return
	if not session:
		return
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		var msg := "UI: [ActivateFailed] Adapter not found for key: %s" % [key]
		Netw.dbg.warn(msg, func(m): push_warning(m))
		return
	
	Netw.dbg.info("UI: [ActivatePanel] %s" % [key])
	var peers: Dictionary = session.get_peers()
	var peer_info: Dictionary = peers.get(peer_key, {})
	var color: Color = peer_info.get("color", Color.WHITE)
	var peer_display: String = peer_info.get("display_name", peer_key)
	
	var panel: Control = _create_panel_control(pt, peer_key)
	var panel_display: String = PanelDataAdapter.PANEL_DISPLAY_NAMES[pt]
	var title_str: String = "%s · %s" % [peer_display, panel_display]
	
	var wrapper := PanelWrapper.new(key, peer_key, title_str, color, panel)
	Netw.dbg.trace(
		"UI: [CreatedWrapper] %s size_flags=%d" % [
			key, wrapper.size_flags_vertical
		]
	)
	wrapper.on_maximize_requested = _on_maximize_requested
	wrapper.status_pressed.connect(
		func(l, s): _show_status_detail(title_str, l, s)
	)
	
	_panel_wrappers[key] = wrapper
	_active_keys.append(key)
	
	# Don't populate yet - the panel hasn't entered the scene tree.
	# _rebuild_grid() will add it to the tree and then populate it.
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


# ─── Grid layout ─────────────────────────────────────────────────────────────

func _rebuild_grid() -> void:
	Netw.dbg.trace(
		"UI: [RebuildGrid] active_count=%d" % [_active_keys.size()]
	)
	# Remove all grid children without freeing them.
	for child: Node in _grid.get_children():
		_grid.remove_child(child)
	
	if not _maximized_key.is_empty() and _maximized_key in _panel_wrappers:
		# Single-panel maximize mode.
		_grid.columns = 1
		_add_wrapper_to_grid(_panel_wrappers[_maximized_key], _maximized_key)
		return
	
	var count: int = _active_keys.size()
	_grid.columns = maxi(ceili(sqrt(float(count))), 1)
	
	for key: String in _active_keys:
		if key in _panel_wrappers:
			_add_wrapper_to_grid(_panel_wrappers[key], key)
		else:
			var msg := "UI: [RebuildFailed] Wrapper missing for key: %s" % [key]
			Netw.dbg.warn(msg, func(m): push_warning(m))


## Adds a wrapper to the grid and populates its panel if it's newly activated.
## [method _ready] fires synchronously on add_child, so all post-ready calls are
## safe after.
func _add_wrapper_to_grid(wrapper: PanelWrapper, key: String) -> void:
	Netw.dbg.trace("UI: [AddChild] %s" % [key])
	_grid.add_child(wrapper)  # triggers _ready() on wrapper and its children
	
	# Initialise peer context now that _ready() has fired on the panel.
	if session:
		var peer_info: Dictionary = session.get_peers().get(wrapper.peer_key, {})
		wrapper.init_peer_context(
			peer_info.get("is_remote", false),
			peer_info.get("online", true),
		)
		
		# Specialized post-ready initialization.
		if wrapper.panel_control is PanelCrashManifest:
			var pcm := wrapper.panel_control as PanelCrashManifest
			pcm._break_btn.set_pressed_no_signal(session.auto_break)
	
	if key in _pending_populate:
		_pending_populate.erase(key)
		if session:
			var adapter: PanelDataAdapter = session.get_adapter(key)
			if adapter:
				wrapper.panel_control.populate(adapter.ring_buffer)
				# Initialise summary tooltips and icons.
				_on_adapter_data_changed(key)


# ─── Maximize / restore ───────────────────────────────────────────────────────

func _on_maximize_requested(key: String) -> void:
	if _maximized_key == key:
		_maximized_key = ""
	else:
		_maximized_key = key
	_rebuild_grid()


# ─── Panel factory ────────────────────────────────────────────────────────────

func _create_panel_control(
	pt: PanelDataAdapter.PanelType,
	peer_key: String
) -> Control:
	var control: Control
	match pt:
		PanelDataAdapter.PanelType.CLOCK:
			control = PanelClock.new()
		
		PanelDataAdapter.PanelType.SPAN:
			control = PanelSpanTracer.new()
			# Inject breakpoint toggle Callable (captures p by reference).
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
					if not se: return
					var ed := se.get_current_editor()
					if not ed: return
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
				var span_name := PanelDataAdapter.PANEL_NAMES[
					PanelDataAdapter.PanelType.SPAN
				]
				var span_key: String = "%s:%s" % [peer_key, span_name]
				if span_key in _panel_wrappers:
					var panel := _panel_wrappers[span_key].panel_control
					(panel as PanelSpanTracer).highlight_cid(cid)
			
			crash_panel.on_auto_break_changed = func(enabled: bool) -> void:
				if session:
					session.set_auto_break(enabled)
			
			crash_panel.on_request_history = func() -> void:
				if session:
					session.send_request_manifest_history(
						session.session_id, peer_key
					)
		
		PanelDataAdapter.PanelType.TOPOLOGY:
			control = PanelTopology.new()
			var topology_panel := control as PanelTopology
			# Node inspect: all known peers are directly reachable via relay.
			topology_panel.on_node_inspect = func(node_path: String) -> void:
				if session:
					session.send_node_inspect(session.session_id, node_path)
			
			topology_panel.on_refresh_requested = func() -> void:
				if session and session.plugin:
					session.plugin.send_to_game(
						session.session_id, "networked:request_snapshot", []
					)
			
			# Nameplate toggle: handled by the panel itself.
			
			topology_panel.on_nameplate_toggled = func(path: String, en: bool) -> void:
				if session:
					session.send_visualizer_toggle(peer_key, path, "nameplate", en)
	
	if control:
		control.size_flags_vertical = Control.SIZE_EXPAND_FILL
		# Ensure panels are visible
		control.custom_minimum_size = Vector2(300, 200)
		return control
	
	return Control.new()


# ─── Breakpoint forwarding ────────────────────────────────────────────────────

## Called by [NetworkedDebuggerPlugin._breakpoint_set_in_tree].
func on_breakpoint_changed(source: String, line: int, enabled: bool) -> void:
	var span_name := PanelDataAdapter.PANEL_NAMES[
		PanelDataAdapter.PanelType.SPAN
	]
	for key: String in _active_keys:
		if not key.ends_with(":" + span_name):
			continue
		if key not in _panel_wrappers:
			continue
		var panel := _panel_wrappers[key].panel_control as PanelSpanTracer
		if panel:
			panel.sync_breakpoint(source, line, enabled)


## Called by [NetworkedDebuggerPlugin._breakpoints_cleared_in_tree].
func on_breakpoints_cleared() -> void:
	var span_name := PanelDataAdapter.PANEL_NAMES[
		PanelDataAdapter.PanelType.SPAN
	]
	for key: String in _active_keys:
		if not key.ends_with(":" + span_name):
			continue
		if key not in _panel_wrappers:
			continue
		var panel := _panel_wrappers[key].panel_control as PanelSpanTracer
		if panel:
			panel.sync_breakpoints_cleared()


# ─── Helpers ─────────────────────────────────────────────────────────────────

static func _make_dot_texture(color: Color) -> ImageTexture:
	var img := Image.create(10, 10, false, Image.FORMAT_RGBA8)
	img.fill(Color.TRANSPARENT)
	var center := Vector2(4.5, 4.5)
	for x: int in range(10):
		for y: int in range(10):
			if Vector2(x, y).distance_to(center) <= 4.0:
				img.set_pixel(x, y, color)
	return ImageTexture.create_from_image(img)
