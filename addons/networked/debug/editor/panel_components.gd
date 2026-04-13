## Component State Inspector panel.
##
## Displays a [Tree] with player nodes as top-level items and one row per
## component type. Data is sourced from the 2 Hz slow-pull heartbeat sent
## by [NetworkedDebugReporter].
##
## Color coding: white = normal, yellow = warning, red = problem.
@tool
class_name PanelComponents
extends VBoxContainer

const C_NORMAL  := Color(1.0, 1.0, 1.0)
const C_WARN    := Color(1.0, 0.85, 0.2)
const C_ERROR   := Color(1.0, 0.35, 0.35)

var _tree: Tree
# player_name → TreeItem
var _player_items: Dictionary[String, TreeItem] = {}


func _ready() -> void:
	var header := Label.new()
	header.text = "Component State (2 Hz)"
	header.add_theme_font_size_override("font_size", 12)
	add_child(header)

	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 3
	_tree.set_column_title(0, "Node / Component")
	_tree.set_column_title(1, "Value")
	_tree.set_column_title(2, "Status")
	_tree.column_titles_visible = true
	_tree.hide_root = true
	_tree.set_column_expand(0, true)
	_tree.set_column_expand(1, true)
	_tree.set_column_expand(2, false)
	_tree.set_column_custom_minimum_width(2, 70)
	add_child(_tree)


func clear() -> void:
	_tree.clear()
	_player_items.clear()


func update_player(d: Dictionary) -> void:
	var player_name: String = d.get("player_name", "?")
	var components: Dictionary = d.get("components", {})

	if not _tree.get_root():
		_tree.create_item()  # invisible root

	var player_item: TreeItem
	if player_name in _player_items:
		player_item = _player_items[player_name]
		# Clear old component children so we rebuild them.
		var child := player_item.get_first_child()
		while child:
			var next := child.get_next()
			child.free()
			child = next
	else:
		player_item = _tree.create_item(_tree.get_root())
		player_item.set_text(0, player_name)
		player_item.set_custom_color(0, Color(0.7, 0.9, 1.0))
		_player_items[player_name] = player_item

	for comp_type: String in components:
		var cdata: Dictionary = components[comp_type]
		_add_component_row(player_item, comp_type, cdata)


func _add_component_row(parent: TreeItem, comp_type: String, d: Dictionary) -> void:
	var row := _tree.create_item(parent)
	row.set_text(0, comp_type)

	match comp_type:
		"ClientComponent": _fill_client(row, d)
		"TPComponent":     _fill_tp(row, d)
		"SaveComponent":   _fill_save(row, d)
		"TickInterpolator":_fill_tick(row, d)
		_:
			row.set_text(1, str(d))


func _fill_client(row: TreeItem, d: Dictionary) -> void:
	var auth_mode: int = d.get("authority_mode", 0)
	var is_auth: bool = d.get("is_multiplayer_authority", false)
	row.set_text(1, "%s  [mode=%d]" % [d.get("username", "?"), auth_mode])
	var status_ok: bool = is_auth == (auth_mode == 0)  # CLIENT mode → should be authority
	row.set_text(2, "✓" if status_ok else "!")
	row.set_custom_color(2, C_NORMAL if status_ok else C_WARN)


func _fill_tp(row: TreeItem, d: Dictionary) -> void:
	var scene: String = d.get("current_scene_name", "—")
	row.set_text(1, scene if not scene.is_empty() else "—")
	row.set_text(2, "idle")
	row.set_custom_color(2, C_NORMAL)


func _fill_save(row: TreeItem, d: Dictionary) -> void:
	var save_dir: String = d.get("save_dir", "?")
	row.set_text(1, save_dir if not save_dir.is_empty() else "—")
	row.set_text(2, "ok")
	row.set_custom_color(2, C_NORMAL)


func _fill_tick(row: TreeItem, d: Dictionary) -> void:
	var lag: float = d.get("display_lag", 0.0)
	var starve: int = d.get("starvation_ticks", 0)
	row.set_text(1, "lag=%.1f  starve=%d" % [lag, starve])
	var color := C_NORMAL
	var status := "ok"
	if starve > 3:
		color = C_ERROR
		status = "starving"
	elif starve > 0:
		color = C_WARN
		status = "warn"
	row.set_text(2, status)
	row.set_custom_color(2, color)
