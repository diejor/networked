## Log Bridge & Span Timeline panel.
##
## Shows Span rows opened via [NetTrace] by the addon's own systems.
## They have an explicit lifecycle indicator: ◉ open (yellow), ✓ closed (green),
## ✗ failed (red). Each step is a child row with elapsed-ms timing.
##
## [b]Breakpoint gutter[/b] (COL_BP): click the breakpoint icon on any row that has
## caller info to toggle a GDScript breakpoint at that call site. The icon stays in
## sync with the script editor via [method sync_breakpoint] /
## [method sync_breakpoints_cleared], driven by
## [NetworkedDebuggerPlugin._breakpoint_set_in_tree].
##
## [b]Jump to source[/b]: double-click any row (or press Enter while selected) to open
## the script at the call site in the editor.
@tool
class_name PanelLogBridge
extends VBoxContainer

## Injected by [NetworkedDebuggerUI] after construction.
## Signature: [code]func(source: String, line: int) -> void[/code]
var toggle_breakpoint: Callable

var _tree: Tree
var _filter_edit: LineEdit
var _filter_text: String = ""

# span_id → TreeItem (span lifecycle rows)
var _span_items: Dictionary[String, TreeItem] = {}
# span_id → open timestamp_usec (elapsed calculation for steps)
var _span_start_usec: Dictionary[String, int] = {}

# Active breakpoints received from _breakpoint_set_in_tree callbacks.
# Key: "source:line". Preserved across clear().
var _active_breakpoints: Dictionary = {}

# "source:line" → Array[TreeItem]. Rebuilt on each push_*; cleared on clear().
var _caller_rows: Dictionary = {}

# Column indices
const COL_BP     := 0  # breakpoint gutter (narrow)
const COL_NAME   := 1  # Operation / Step
const COL_TREE   := 2
const COL_PLAYER := 3
const COL_ELAPSED := 4


func _ready() -> void:
	var header_row := HBoxContainer.new()
	add_child(header_row)

	var title := Label.new()
	title.text = "Log Bridge"
	title.add_theme_font_size_override("font_size", 12)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header_row.add_child(title)

	_filter_edit = LineEdit.new()
	_filter_edit.placeholder_text = "Filter events…"
	_filter_edit.custom_minimum_size.x = 160
	_filter_edit.text_changed.connect(func(v: String) -> void: _filter_text = v)
	header_row.add_child(_filter_edit)

	var clear_btn := Button.new()
	clear_btn.text = "Clear"
	clear_btn.pressed.connect(clear)
	header_row.add_child(clear_btn)

	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 5
	_tree.set_column_title(COL_BP, "")
	_tree.set_column_title(COL_NAME, "Operation / Step")
	_tree.set_column_title(COL_TREE, "Tree")
	_tree.set_column_title(COL_PLAYER, "Player")
	_tree.set_column_title(COL_ELAPSED, "Elapsed")
	_tree.column_titles_visible = true
	_tree.hide_root = true
	_tree.set_column_expand(COL_BP, false)
	_tree.set_column_expand(COL_NAME, true)
	_tree.set_column_expand(COL_TREE, false)
	_tree.set_column_expand(COL_PLAYER, false)
	_tree.set_column_expand(COL_ELAPSED, false)
	_tree.set_column_custom_minimum_width(COL_BP, 24)
	_tree.set_column_custom_minimum_width(COL_TREE, 60)
	_tree.set_column_custom_minimum_width(COL_PLAYER, 90)
	_tree.set_column_custom_minimum_width(COL_ELAPSED, 70)
	_tree.item_activated.connect(_on_item_activated)
	_tree.button_clicked.connect(_on_bp_button_clicked)
	add_child(_tree)


func clear() -> void:
	_tree.clear()
	_span_items.clear()
	_span_start_usec.clear()
	_caller_rows.clear()
	# _active_breakpoints intentionally preserved — configuration survives tree switches.


## Scroll to and highlight the span row matching [param cid].
## Called by the Orchestrator Bus when the user selects a manifest entry.
func highlight_cid(cid: String) -> void:
	if cid.is_empty():
		return
	_clear_highlights()
	var target: TreeItem = _span_items.get(cid)
	if not target:
		for k: String in _span_items:
			if k.begins_with(cid) or cid.begins_with(k):
				target = _span_items[k]
				break
	if target:
		target.set_custom_bg_color(COL_NAME, Color(0.2, 0.4, 0.2), false)
		target.select(COL_NAME)
		_tree.scroll_to_item(target)


func _clear_highlights() -> void:
	var root := _tree.get_root()
	if not root:
		return
	var item := root.get_first_child()
	while item:
		item.clear_custom_bg_color(COL_NAME)
		item = item.get_next()


# ─── Breakpoint Sync (called from NetworkedDebuggerUI) ────────────────────────

## Updates gutter state for all rows at [param source]:[param line].
## Called whenever the script editor adds or removes a breakpoint.
func sync_breakpoint(source: String, line: int, enabled: bool) -> void:
	var key := "%s:%d" % [source, line]
	if enabled:
		_active_breakpoints[key] = true
	else:
		_active_breakpoints.erase(key)
	for item: TreeItem in _caller_rows.get(key, []):
		_refresh_bp_cell(item, enabled)


## Clears all gutter indicators. Called when the editor removes all breakpoints.
func sync_breakpoints_cleared() -> void:
	_active_breakpoints.clear()
	for key: String in _caller_rows:
		for item: TreeItem in _caller_rows[key]:
			_refresh_bp_cell(item, false)


# ─── Span Lifecycle (NetTrace) ────────────────────────────────────────────────

## Creates a top-level span row with an open-state indicator (yellow ◉).
func push_span_open(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	if span_id.is_empty():
		return
	var label: String = d.get("label", "span")
	if not _filter_text.is_empty() and not label.contains(_filter_text):
		return

	if not _tree.get_root():
		_tree.create_item()

	var item := _tree.create_item(_tree.get_root())
	item.set_text(COL_NAME, "◉ %s" % label)
	item.set_text(COL_TREE, d.get("tree_name", ""))
	item.set_text(COL_PLAYER, "")
	item.set_text(COL_ELAPSED, "f%d" % d.get("frame", 0))
	item.set_custom_color(COL_NAME, Color(1.0, 0.85, 0.2))  # yellow = open

	var caller: Dictionary = d.get("caller", {})
	item.set_metadata(COL_NAME, {"span_id": span_id, "caller": caller})
	_register_caller_row(item, caller)

	_span_items[span_id] = item
	_span_start_usec[span_id] = d.get("timestamp_usec", Time.get_ticks_usec())

	var peers: Array = d.get("affected_peers", [])
	if not peers.is_empty():
		var p_row := _tree.create_item(item)
		p_row.set_text(COL_NAME, "  peers: %s" % str(peers))
		p_row.set_custom_color(COL_NAME, Color(0.55, 0.55, 0.55))
		_set_unselectable(p_row)


## Appends a step child row to an existing span row.
func push_span_step(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	var item: TreeItem = _span_items.get(span_id)
	if not item:
		return
	var s: Dictionary = d.get("step", {})
	var step_label: String = s.get("label", "?")
	var step_usec: int = s.get("usec", 0)
	var elapsed_ms: float = (step_usec - _span_start_usec.get(span_id, step_usec)) / 1000.0

	var row := _tree.create_item(item)
	row.set_text(COL_NAME, "   %s" % step_label)
	row.set_text(COL_ELAPSED, "+%.1f ms" % elapsed_ms)
	var step_data: Dictionary = s.get("data", {})
	if not step_data.is_empty():
		row.set_tooltip_text(COL_NAME, str(step_data))

	var caller: Dictionary = s.get("caller", {})
	row.set_metadata(COL_NAME, {"caller": caller})
	_register_caller_row(row, caller)

	item.set_collapsed(false)


## Updates a span row to a clean-close state (green ✓).
func push_span_close(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	var item: TreeItem = _span_items.get(span_id)
	if not item:
		return
	item.set_text(COL_NAME, "✓ %s" % d.get("label", item.get_text(COL_NAME).substr(2)))
	item.set_custom_color(COL_NAME, Color(0.3, 0.85, 0.4))  # green = ok
	var elapsed_usec: int = d.get("elapsed_usec", 0)
	if elapsed_usec > 0:
		item.set_text(COL_ELAPSED, "%.1f ms" % (elapsed_usec / 1000.0))


## Updates a span row to a failed state (red ✗) and appends the failure reason.
func push_span_fail(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	var item: TreeItem = _span_items.get(span_id)
	if not item:
		return
	var label: String = d.get("label", item.get_text(COL_NAME).substr(2))
	var reason: String = d.get("reason", "?")
	item.set_text(COL_NAME, "✗ %s  [%s]" % [label, reason])
	item.set_custom_color(COL_NAME, Color(1.0, 0.35, 0.35))  # red = failed
	var elapsed_usec: int = d.get("elapsed_usec", 0)
	if elapsed_usec > 0:
		item.set_text(COL_ELAPSED, "%.1f ms" % (elapsed_usec / 1000.0))

	var r := _tree.create_item(item)
	r.set_text(COL_NAME, "  ✗ %s" % reason)
	r.set_custom_color(COL_NAME, Color(1.0, 0.45, 0.45))
	var fail_caller: Dictionary = d.get("caller", {})
	r.set_metadata(COL_NAME, {"caller": fail_caller})
	_register_caller_row(r, fail_caller)
	if fail_caller.is_empty():
		_set_unselectable(r)
	item.set_collapsed(false)


# ─── Internal ─────────────────────────────────────────────────────────────────

## Registers [param item] in [member _caller_rows] and adds the breakpoint button.
## Does nothing if [param caller] is empty (no source info available).
func _register_caller_row(item: TreeItem, caller: Dictionary) -> void:
	if caller.is_empty():
		return
	var source: String = caller.get("source", "")
	var line: int = caller.get("line", 0)
	if source.is_empty():
		return

	var key := "%s:%d" % [source, line]
	if key not in _caller_rows:
		_caller_rows[key] = []
	(_caller_rows[key] as Array).append(item)

	var icon := EditorInterface.get_base_control().get_theme_icon("Breakpoint", "EditorIcons")
	item.add_button(COL_BP, icon, 0, false, "")
	_refresh_bp_cell(item, _active_breakpoints.get(key, false))

	item.set_tooltip_text(COL_NAME, "%s:%d" % [source.get_file(), line])


func _refresh_bp_cell(item: TreeItem, active: bool) -> void:
	if item.get_button_count(COL_BP) == 0:
		return
	item.set_button_color(COL_BP, 0,
		Color(1.0, 1.0, 1.0, 1.0) if active else Color(1.0, 1.0, 1.0, 0.3))


func _on_item_activated() -> void:
	var item := _tree.get_selected()
	if not item:
		return
	var meta = item.get_metadata(COL_NAME)
	if not meta is Dictionary:
		return
	var caller: Dictionary = (meta as Dictionary).get("caller", {})
	var source: String = caller.get("source", "")
	if source.is_empty():
		return
	var script = load(source)
	if script is Script:
		EditorInterface.set_main_screen_editor("Script")
		EditorInterface.edit_script(script, caller.get("line", 0))


func _on_bp_button_clicked(item: TreeItem, column: int, id: int, _mouse_button_index: int) -> void:
	if column != COL_BP or id != 0:
		return
	var meta = item.get_metadata(COL_NAME)
	if not meta is Dictionary:
		return
	var caller: Dictionary = (meta as Dictionary).get("caller", {})
	var source: String = caller.get("source", "")
	if source.is_empty() or not toggle_breakpoint.is_valid():
		return
	toggle_breakpoint.call(source, caller.get("line", 0))


func _set_unselectable(item: TreeItem) -> void:
	for col in range(_tree.columns):
		item.set_selectable(col, false)
