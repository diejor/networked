## Log Bridge & Span Timeline panel.
##
## Shows two kinds of rows in a unified Tree:
##
## [b]Span rows[/b] — opened via [NetTrace] by the addon's own systems.
## They have an explicit lifecycle indicator: ◉ open (yellow), ✓ closed (green),
## ✗ failed (red). Each step is a child row with elapsed-ms timing.
##
## [b]Component event rows[/b] — emitted via [method NetComponent._emit_debug_event].
## They are grouped by [code]correlation_id[/code] as before.
@tool
class_name PanelLogBridge
extends VBoxContainer

var _tree: Tree
var _filter_edit: LineEdit
var _filter_text: String = ""

# correlation_id → TreeItem (component-event group rows)
var _op_items: Dictionary[String, TreeItem] = {}
# correlation_id → first event timestamp_usec (elapsed calculation)
var _op_start_usec: Dictionary[String, int] = {}

# span_id → TreeItem (span lifecycle rows)
var _span_items: Dictionary[String, TreeItem] = {}
# span_id → open timestamp_usec (elapsed calculation for steps)
var _span_start_usec: Dictionary[String, int] = {}


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
	_tree.columns = 4
	_tree.set_column_title(0, "Operation / Step")
	_tree.set_column_title(1, "Tree")
	_tree.set_column_title(2, "Player")
	_tree.set_column_title(3, "Elapsed")
	_tree.column_titles_visible = true
	_tree.hide_root = true
	_tree.set_column_expand(0, true)
	_tree.set_column_expand(1, false)
	_tree.set_column_expand(2, false)
	_tree.set_column_expand(3, false)
	_tree.set_column_custom_minimum_width(1, 60)
	_tree.set_column_custom_minimum_width(2, 90)
	_tree.set_column_custom_minimum_width(3, 70)
	add_child(_tree)


func clear() -> void:
	_tree.clear()
	_op_items.clear()
	_op_start_usec.clear()
	_span_items.clear()
	_span_start_usec.clear()


## Scroll to and highlight the operation row matching [param cid].
## Called by the Orchestrator Bus when the user selects a manifest entry.
func highlight_cid(cid: String) -> void:
	if cid.is_empty():
		return
	# First, clear any prior highlights.
	_clear_highlights()
	var target: TreeItem = _op_items.get(cid)
	if not target:
		# Try prefix match (manifest CID may be the full value, stored item may use it as key).
		for k: String in _op_items:
			if k.begins_with(cid) or cid.begins_with(k):
				target = _op_items[k]
				break
	if target:
		target.set_custom_bg_color(0, Color(0.2, 0.4, 0.2), false)
		target.select(0)
		_tree.scroll_to_item(target)


func _clear_highlights() -> void:
	var root := _tree.get_root()
	if not root:
		return
	var item := root.get_first_child()
	while item:
		item.clear_custom_bg_color(0)
		item = item.get_next()


func push_event(d: Dictionary) -> void:
	var event_type: String = d.get("event_type", "?")
	if not _filter_text.is_empty() and not event_type.contains(_filter_text):
		return

	if not _tree.get_root():
		_tree.create_item()  # invisible root

	var cid: String = d.get("correlation_id", "")
	var ts: int = d.get("timestamp_usec", 0)
	var tree_tag: String = d.get("tree_name", "?")
	var side: String = d.get("side", "?")
	var player: String = d.get("player_name", "")
	var data: Dictionary = d.get("data", {})

	if cid.is_empty():
		# Standalone event.
		var item := _tree.create_item(_tree.get_root())
		item.set_text(0, event_type)
		item.set_text(1, "[%s]" % tree_tag)
		item.set_text(2, player)
		item.set_text(3, "—")
		if not data.is_empty():
			item.set_tooltip_text(0, str(data))
		return

	# Grouped operation.
	var op_item: TreeItem
	if cid in _op_items:
		op_item = _op_items[cid]
		# Update op summary to reflect latest step.
		op_item.set_text(0, "⯈ %s  [%s]" % [cid.substr(0, 16), event_type])
	else:
		op_item = _tree.create_item(_tree.get_root())
		op_item.set_text(0, "⯈ %s  [%s]" % [cid.substr(0, 16), event_type])
		op_item.set_text(1, "[%s]" % side)
		op_item.set_text(2, player)
		op_item.set_text(3, "")
		op_item.set_custom_color(0, Color(0.7, 0.9, 1.0))
		_op_items[cid] = op_item
		_op_start_usec[cid] = ts

	# Step child.
	var elapsed_ms := (ts - _op_start_usec[cid]) / 1000.0
	var step := _tree.create_item(op_item)
	step.set_text(0, "   %s" % event_type)
	step.set_text(1, "[%s]" % side)
	step.set_text(2, player)
	step.set_text(3, "+%.1f ms" % elapsed_ms)
	if not data.is_empty():
		step.set_tooltip_text(0, str(data))

	# Keep operation expanded by default.
	op_item.set_collapsed(false)


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
	item.set_text(0, "◉ %s" % label)
	item.set_text(1, "")
	item.set_text(2, "")
	item.set_text(3, "f%d" % d.get("frame", 0))
	item.set_custom_color(0, Color(1.0, 0.85, 0.2))  # yellow = open
	item.set_metadata(0, {"span_id": span_id})
	_span_items[span_id] = item
	_span_start_usec[span_id] = d.get("timestamp_usec", Time.get_ticks_usec())

	var peers: Array = d.get("affected_peers", [])
	if not peers.is_empty():
		var p_row := _tree.create_item(item)
		p_row.set_text(0, "  peers: %s" % str(peers))
		p_row.set_custom_color(0, Color(0.55, 0.55, 0.55))
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
	row.set_text(0, "   %s" % step_label)
	row.set_text(3, "+%.1f ms" % elapsed_ms)
	var step_data: Dictionary = s.get("data", {})
	if not step_data.is_empty():
		row.set_tooltip_text(0, str(step_data))
	_set_unselectable(row)
	item.set_collapsed(false)


## Updates a span row to a clean-close state (green ✓).
func push_span_close(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	var item: TreeItem = _span_items.get(span_id)
	if not item:
		return
	item.set_text(0, "✓ %s" % d.get("label", item.get_text(0).substr(2)))
	item.set_custom_color(0, Color(0.3, 0.85, 0.4))  # green = ok
	var elapsed_usec: int = d.get("elapsed_usec", 0)
	if elapsed_usec > 0:
		item.set_text(3, "%.1f ms" % (elapsed_usec / 1000.0))


## Updates a span row to a failed state (red ✗) and appends the failure reason.
func push_span_fail(d: Dictionary) -> void:
	var span_id: String = d.get("id", "")
	var item: TreeItem = _span_items.get(span_id)
	if not item:
		return
	var label: String = d.get("label", item.get_text(0).substr(2))
	var reason: String = d.get("reason", "?")
	item.set_text(0, "✗ %s  [%s]" % [label, reason])
	item.set_custom_color(0, Color(1.0, 0.35, 0.35))  # red = failed
	var elapsed_usec: int = d.get("elapsed_usec", 0)
	if elapsed_usec > 0:
		item.set_text(3, "%.1f ms" % (elapsed_usec / 1000.0))

	var r := _tree.create_item(item)
	r.set_text(0, "  ✗ %s" % reason)
	r.set_custom_color(0, Color(1.0, 0.45, 0.45))
	_set_unselectable(r)
	item.set_collapsed(false)


func _set_unselectable(item: TreeItem) -> void:
	for col in range(_tree.columns):
		item.set_selectable(col, false)

	# Keep operation expanded by default.
	op_item.set_collapsed(false)
