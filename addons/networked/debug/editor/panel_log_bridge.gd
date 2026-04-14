## Log Bridge & Trace List panel.
##
## Groups structured [method NetComponent._emit_debug_event] calls by their
## [code]correlation_id[/code] so multi-step operations (teleport, join) appear
## as a single expandable row with per-step timestamps.
##
## Events without a correlation_id appear as standalone rows.
@tool
class_name PanelLogBridge
extends VBoxContainer

var _tree: Tree
var _filter_edit: LineEdit
var _filter_text: String = ""

# correlation_id → TreeItem (top-level operation row)
var _op_items: Dictionary[String, TreeItem] = {}
# correlation_id → first event timestamp_usec (for elapsed calculation)
var _op_start_usec: Dictionary[String, int] = {}


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
