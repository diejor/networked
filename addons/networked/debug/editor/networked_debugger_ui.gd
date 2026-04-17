## Main "Networked" tab in the editor debugger.
##
## A [DebuggerSession] injected by [NetworkedDebuggerPlugin] owns all data.
## This class reacts to session signals and drives the layout.
## [br]
## Checking a child creates a [PanelWrapper] in the right grid and calls
## [method populate] on the panel immediately with existing buffered data.
## [br]
## Right grid: column count auto-adjusts to [code]ceil(sqrt(active_count))[/code].
## Double-clicking a PanelWrapper title bar maximizes it; double-clicking again restores.
@tool
class_name NetworkedDebuggerUI
extends VBoxContainer

## Injected by [NetworkedDebuggerPlugin] before the node enters the scene tree.
var session: DebuggerSession

# ─── Layout nodes ────────────────────────────────────────────────────────────
var _peer_tree: Tree
var _grid: GridContainer
var _scroll: ScrollContainer
var _split: HSplitContainer

# ─── State ───────────────────────────────────────────────────────────────────
# tree_name -> bold TreeItem (non-selectable peer header row)
var _peer_tree_items: Dictionary[String, TreeItem] = {}

# adapter_key -> PanelWrapper node currently in the grid
var _panel_wrappers: Dictionary[String, PanelWrapper] = {}

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
	session.adapter_data_changed.connect(_on_adapter_data_changed)
	session.session_cleared.connect(_on_session_cleared)


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_apply_theme_styles()


## Applies editor-theme-aware styles to the tree panel and scroll area.
## Re-runs on NOTIFICATION_THEME_CHANGED so light/dark switches update live.
func _apply_theme_styles() -> void:
	if not is_inside_tree() or not _peer_tree:
		return
	
	# Left tree: slightly darker than the base background, with rounded corners.
	var tree_bg := StyleBoxFlat.new()
	tree_bg.bg_color = get_theme_color("dark_color_2", "Editor")
	tree_bg.set_corner_radius_all(4)
	_peer_tree.add_theme_stylebox_override("panel", tree_bg)
	
	# Scroll area background: slightly lighter than the tree, rounding matches.
	var scroll_bg := StyleBoxFlat.new()
	scroll_bg.bg_color = get_theme_color("base_color", "Editor")
	scroll_bg.set_corner_radius_all(4)
	# ScrollContainer uses "panel" for its backdrop in the editor theme.
	_scroll.add_theme_stylebox_override("panel", scroll_bg)


# ─── Layout construction ──────────────────────────────────────────────────────

func _build_layout() -> void:
	_split = HSplitContainer.new()
	_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_split.split_offset = 260
	add_child(_split)

	_peer_tree = Tree.new()
	_peer_tree.custom_minimum_size.x = 240
	_peer_tree.hide_root = true
	_peer_tree.columns = 2
	_peer_tree.set_column_title(0, "Peer")
	_peer_tree.set_column_title(1, "ID")
	_peer_tree.column_titles_visible = true
	_peer_tree.set_column_expand(0, true)
	_peer_tree.set_column_expand(1, false)
	_peer_tree.set_column_custom_minimum_width(1, 40)
	_peer_tree.item_edited.connect(_on_peer_tree_item_edited)
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
	_scroll.add_child(_grid)


# ─── Session signal handlers ──────────────────────────────────────────────────

func _on_peer_registered(tree_name: String, is_server: bool, color: Color) -> void:
	# Root must exist.
	if not _peer_tree.get_root():
		_peer_tree.create_item()

	var peer_item := _peer_tree.create_item(_peer_tree.get_root())
	var prefix: String = "[S] " if is_server else "[C] "
	peer_item.set_text(0, prefix + tree_name)
	# Server is always peer 1; clients get their ID lazily from peer_id_resolved.
	peer_item.set_text(1, "1" if is_server else "—")
	peer_item.set_custom_color(0, color)
	peer_item.set_custom_color(1, color)
	peer_item.set_icon(0, _dot_online)
	peer_item.set_selectable(0, false)
	peer_item.set_selectable(1, false)

	var font: Font = peer_item.get_tree().get_theme_font(&"bold", &"Tree") if peer_item.get_tree() else null
	if font:
		peer_item.set_custom_font(0, font)

	_peer_tree_items[tree_name] = peer_item

	# Three checkbox children.
	for pt: PanelDataAdapter.PanelType in [
		PanelDataAdapter.PanelType.SPAN,
		PanelDataAdapter.PanelType.CLOCK,
		PanelDataAdapter.PanelType.CRASH,
	]:
		var child := _peer_tree.create_item(peer_item)
		child.set_cell_mode(0, TreeItem.CELL_MODE_CHECK)
		child.set_text(0, PanelDataAdapter.PANEL_DISPLAY_NAMES[pt])
		child.set_editable(0, true)
		child.set_checked(0, false)
		child.set_selectable(1, false)
		child.set_metadata(0, {
			"tree_name": tree_name,
			"panel_type": pt,
		})

	peer_item.set_collapsed(false)


func _on_peer_status_changed(tree_name: String, online: bool) -> void:
	if tree_name not in _peer_tree_items:
		return
	var peer_item: TreeItem = _peer_tree_items[tree_name]
	peer_item.set_icon(0, _dot_online if online else _dot_offline)

	# Walk checkbox children and update editability.
	var child := peer_item.get_first_child()
	while child:
		child.set_editable(0, online)
		if not online:
			child.set_custom_color(0, Color(0.5, 0.5, 0.5))
		else:
			child.clear_custom_color(0)
		child = child.get_next()


func _on_adapter_data_changed(key: String) -> void:
	if key not in _panel_wrappers:
		return
	var wrapper: PanelWrapper = _panel_wrappers[key]
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter or adapter.ring_buffer.is_empty():
		return
	
	wrapper.update_live_metric(adapter.get_current_label())
	wrapper.panel_control.on_new_entry(adapter.ring_buffer[-1])
	
	# Cross-panel sync: crash arrival highlights the peer's Span Tracer (if open).
	if key.ends_with(":" + PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.CRASH]):
		var cid: String = (adapter.ring_buffer[-1] as Dictionary).get("cid", "")
		if not cid.is_empty():
			var span_key: String = key.replace(
				":" + PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.CRASH],
				":" + PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]
			)
			if span_key in _panel_wrappers:
				(_panel_wrappers[span_key].panel_control as PanelLogBridge).highlight_cid(cid)


func _on_session_cleared() -> void:
	# Deactivate all panels and remove tree items.
	for key: String in _active_keys.duplicate():
		_deactivate_panel(key)
	_active_keys.clear()
	_panel_wrappers.clear()
	_peer_tree_items.clear()
	_peer_tree.clear()
	_peer_tree.create_item()  # re-create invisible root
	_maximized_key = ""
	_rebuild_grid()


# ─── Checkbox toggle ──────────────────────────────────────────────────────────

func _on_peer_tree_item_edited() -> void:
	var item: TreeItem = _peer_tree.get_edited()
	if not item:
		return
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var m: Dictionary = meta as Dictionary
	var tn: String = m.get("tree_name", "")
	var pt: PanelDataAdapter.PanelType = m.get("panel_type", PanelDataAdapter.PanelType.CLOCK)
	var key: String = "%s:%s" % [tn, PanelDataAdapter.PANEL_NAMES[pt]]
	var checked: bool = item.is_checked(0)

	if checked:
		_activate_panel(key, tn, pt)
	else:
		_deactivate_panel(key)
	_rebuild_grid()


func _activate_panel(key: String, tree_name: String, pt: PanelDataAdapter.PanelType) -> void:
	if key in _panel_wrappers:
		return
	if not session:
		return
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		return

	var peers: Dictionary = session.get_peers()
	var peer_info: Dictionary = peers.get(tree_name, {})
	var color: Color = peer_info.get("color", Color.WHITE)

	var panel: Control = _create_panel_control(pt, tree_name)
	var display_name: String = PanelDataAdapter.PANEL_DISPLAY_NAMES[pt]
	var title_str: String = "%s · %s" % [tree_name, display_name]

	var wrapper := PanelWrapper.new(key, title_str, color, panel)
	wrapper.on_maximize_requested = _on_maximize_requested
	# Wire "Break on Manifest" for crash panels.
	# Restore the persisted break state so the button reflects the current value
	# even after a game restart that triggered session_cleared.
	if pt == PanelDataAdapter.PanelType.CRASH:
		var crash_panel := panel as PanelCrashManifest
		if crash_panel:
			crash_panel.on_auto_break_changed = func(enabled: bool) -> void:
				if session:
					session.set_auto_break(enabled)
			wrapper.add_break_button(crash_panel.on_auto_break_changed,
				session.auto_break if session else false)

	_panel_wrappers[key] = wrapper
	_active_keys.append(key)

	# Don't populate yet — the panel hasn't entered the scene tree (_ready() not called).
	# _rebuild_grid() will add it to the tree and then populate it.
	_pending_populate[key] = null  # sentinel; adapter buffer read at populate time


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


## Adds a wrapper to the grid and populates its panel if it's newly activated.
## _ready() fires synchronously on add_child, so populate() is safe to call after.
func _add_wrapper_to_grid(wrapper: PanelWrapper, key: String) -> void:
	_grid.add_child(wrapper)  # triggers _ready() on wrapper and its children
	if key in _pending_populate:
		_pending_populate.erase(key)
		if session:
			var adapter: PanelDataAdapter = session.get_adapter(key)
			if adapter:
				wrapper.panel_control.populate(adapter.ring_buffer)
				wrapper.update_live_metric(adapter.get_current_label())


# ─── Maximize / restore ───────────────────────────────────────────────────────

func _on_maximize_requested(key: String) -> void:
	if _maximized_key == key:
		_maximized_key = ""
	else:
		_maximized_key = key
	_rebuild_grid()


# ─── Panel factory ────────────────────────────────────────────────────────────

func _create_panel_control(pt: PanelDataAdapter.PanelType, tree_name: String) -> Control:
	match pt:
		PanelDataAdapter.PanelType.CLOCK:
			var p := PanelClock.new()
			p.size_flags_vertical = Control.SIZE_EXPAND_FILL
			return p

		PanelDataAdapter.PanelType.SPAN:
			var p := PanelLogBridge.new()
			p.size_flags_vertical = Control.SIZE_EXPAND_FILL
			# Inject breakpoint toggle Callable (captures p by reference).
			p.toggle_breakpoint = func(source: String, line: int) -> void:
				var script: Script = load(source) as Script
				if not script:
					return
				var new_state: bool = not bool(p._active_breakpoints.get(
					"%s:%d" % [source, line], false))
				p.sync_breakpoint(source, line, new_state)
				EditorInterface.set_main_screen_editor("Script")
				EditorInterface.edit_script(script, line)
				(func() -> void:
					var se := EditorInterface.get_script_editor()
					if not se: return
					var ed := se.get_current_editor()
					if not ed: return
					var ce := ed.get_base_editor() as CodeEdit
					if ce:
						ce.set_line_as_breakpoint(line - 1, new_state)
				).call_deferred()
			return p

		PanelDataAdapter.PanelType.CRASH:
			var p := PanelCrashManifest.new()
			p.size_flags_vertical = Control.SIZE_EXPAND_FILL
			# Cross-panel context selection: highlight the peer's span tracer.
			p.on_context_selected = func(ctx: Dictionary) -> void:
				var cid: String = ctx.get("cid", "")
				if cid.is_empty():
					return
				var span_key: String = "%s:%s" % [
					tree_name,
					PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN],
				]
				if span_key in _panel_wrappers:
					(_panel_wrappers[span_key].panel_control as PanelLogBridge).highlight_cid(cid)
			return p

	return Control.new()


# ─── Breakpoint forwarding ────────────────────────────────────────────────────

## Called by [NetworkedDebuggerPlugin._breakpoint_set_in_tree].
func on_breakpoint_changed(source: String, line: int, enabled: bool) -> void:
	for key: String in _active_keys:
		if not key.ends_with(":" + PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]):
			continue
		if key not in _panel_wrappers:
			continue
		var panel: PanelLogBridge = _panel_wrappers[key].panel_control as PanelLogBridge
		if panel:
			panel.sync_breakpoint(source, line, enabled)


## Called by [NetworkedDebuggerPlugin._breakpoints_cleared_in_tree].
func on_breakpoints_cleared() -> void:
	for key: String in _active_keys:
		if not key.ends_with(":" + PanelDataAdapter.PANEL_NAMES[PanelDataAdapter.PanelType.SPAN]):
			continue
		if key not in _panel_wrappers:
			continue
		var panel: PanelLogBridge = _panel_wrappers[key].panel_control as PanelLogBridge
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


# ─── PanelWrapper inner class ─────────────────────────────────────────────────

## Wraps a panel Control in a titled container with a colored header bar.
## Exposes [member on_maximize_requested] for the UI's maximize/restore logic.
## Draws a rounded editor-themed background using [method Control._draw].
class PanelWrapper extends VBoxContainer:
	## Called with [member adapter_key] when the title bar is double-clicked.
	var on_maximize_requested: Callable

	var adapter_key: String
	var panel_control: Control

	var _peer_color: Color    ## Stored for background style rebuild on theme change.
	var _title_label: Label
	var _metric_label: Label
	var _title_bar: HBoxContainer
	var _title_panel: PanelContainer
	var _outer_style: StyleBoxFlat  ## Cached outer rounded-border background.


	func _init(
		p_key: String,
		p_title: String,
		p_color: Color,
		p_panel: Control,
	) -> void:
		adapter_key = p_key
		panel_control = p_panel
		_peer_color = p_color
		size_flags_horizontal = Control.SIZE_EXPAND_FILL
		size_flags_vertical = Control.SIZE_EXPAND_FILL

		# Title bar: PanelContainer with a peer-colored StyleBoxFlat background.
		_title_panel = PanelContainer.new()
		_title_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var title_style := StyleBoxFlat.new()
		title_style.bg_color = Color(p_color.r, p_color.g, p_color.b, 0.15)
		title_style.corner_radius_top_left = 4
		title_style.corner_radius_top_right = 4
		_title_panel.add_theme_stylebox_override("panel", title_style)
		_title_panel.gui_input.connect(_on_title_bar_gui_input)
		_title_panel.mouse_filter = Control.MOUSE_FILTER_STOP
		add_child(_title_panel)

		_title_bar = HBoxContainer.new()
		_title_bar.add_theme_constant_override("separation", 8)
		_title_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_title_panel.add_child(_title_bar)

		_title_label = Label.new()
		_title_label.text = p_title
		_title_label.add_theme_color_override("font_color", p_color)
		_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_title_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_title_bar.add_child(_title_label)

		_metric_label = Label.new()
		_metric_label.add_theme_color_override("font_color", p_color)
		_metric_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_title_bar.add_child(_metric_label)

		panel_control.size_flags_vertical = Control.SIZE_EXPAND_FILL
		add_child(panel_control)


	func _notification(what: int) -> void:
		match what:
			NOTIFICATION_READY, NOTIFICATION_THEME_CHANGED:
				_rebuild_outer_style()
			NOTIFICATION_RESIZED:
				queue_redraw()


	## Draws a rounded background using the editor theme's dark panel color.
	## The peer color provides a subtle border tint.
	func _draw() -> void:
		if _outer_style:
			draw_style_box(_outer_style, Rect2(Vector2.ZERO, size))


	func _rebuild_outer_style() -> void:
		if not is_inside_tree():
			return
		_outer_style = StyleBoxFlat.new()
		_outer_style.bg_color = get_theme_color("dark_color_1", "Editor")
		_outer_style.border_color = Color(_peer_color.r, _peer_color.g, _peer_color.b, 0.35)
		_outer_style.set_border_width_all(1)
		_outer_style.set_corner_radius_all(4)
		_outer_style.content_margin_left   = 2
		_outer_style.content_margin_right  = 2
		_outer_style.content_margin_top    = 2
		_outer_style.content_margin_bottom = 2
		queue_redraw()


	## Adds a "Break on Manifest" toggle to the title bar (crash panels only).
	## [param initial] restores the persisted state without firing [param on_toggled].
	func add_break_button(on_toggled: Callable, initial: bool = false) -> void:
		var btn := CheckButton.new()
		btn.text = "Break"
		btn.tooltip_text = "Pause the game the moment a crash manifest arrives for this peer."
		# Apply initial state silently — avoids a redundant set_auto_break() round-trip.
		btn.set_pressed_no_signal(initial)
		btn.toggled.connect(on_toggled)
		btn.mouse_filter = Control.MOUSE_FILTER_STOP
		_title_bar.add_child(btn)


	func update_live_metric(text: String) -> void:
		_metric_label.text = text


	func _on_title_bar_gui_input(event: InputEvent) -> void:
		var mb := event as InputEventMouseButton
		if mb and mb.double_click and mb.button_index == MOUSE_BUTTON_LEFT:
			if on_maximize_requested.is_valid():
				on_maximize_requested.call(adapter_key)
