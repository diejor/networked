## Matrices panel — two GridContainer-based truth tables.
##
## [b]Visibility Matrix[/b]: rows = lobbies, columns = connected peers.
## Each cell shows whether that peer is in that lobby's [code]connected_clients[/code].
##
## [b]Replication Matrix[/b]: rows = replicated properties, columns = connected peers.
## Populated on demand when the user selects a node and clicks Watch.
@tool
class_name PanelMatrices
extends VBoxContainer

const C_OK     := Color(0.18, 0.65, 0.18)
const C_BLOCK  := Color(0.75, 0.55, 0.10)
const C_FAIL   := Color(0.65, 0.18, 0.18)
const C_SERVER := Color(0.20, 0.40, 0.65)
const C_EMPTY  := Color(0.20, 0.20, 0.20)

## Callable set by the parent UI so this panel can send watch_node messages.
var send_to_game: Callable

var _vis_grid: GridContainer
var _rep_grid: GridContainer
var _watch_path_edit: LineEdit
var _watch_btn: Button

# Latest lobby snapshot for rebuilding.
var _last_snapshot: Dictionary = {}
# Sorted peer IDs derived from snapshot.
var _all_peers: Array[int] = []


func _ready() -> void:
	add_theme_constant_override("separation", 6)

	# ── Visibility Matrix ──────────────────────────────────────
	var vis_section := VBoxContainer.new()
	vis_section.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(vis_section)

	var vis_header := Label.new()
	vis_header.text = "Visibility Matrix (lobby × peer)"
	vis_header.add_theme_font_size_override("font_size", 12)
	vis_section.add_child(vis_header)

	var vis_scroll := ScrollContainer.new()
	vis_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vis_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	vis_section.add_child(vis_scroll)

	_vis_grid = GridContainer.new()
	_vis_grid.columns = 1
	vis_scroll.add_child(_vis_grid)

	# ── Replication Matrix ─────────────────────────────────────
	var rep_section := VBoxContainer.new()
	rep_section.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(rep_section)

	var rep_header := Label.new()
	rep_header.text = "Replication Matrix (property × peer)"
	rep_header.add_theme_font_size_override("font_size", 12)
	rep_section.add_child(rep_header)

	var watch_bar := HBoxContainer.new()
	rep_section.add_child(watch_bar)

	var watch_lbl := Label.new()
	watch_lbl.text = "Node path:"
	watch_bar.add_child(watch_lbl)

	_watch_path_edit = LineEdit.new()
	_watch_path_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_watch_path_edit.placeholder_text = "/root/Game/Player"
	watch_bar.add_child(_watch_path_edit)

	_watch_btn = Button.new()
	_watch_btn.text = "Watch"
	_watch_btn.pressed.connect(_on_watch_pressed)
	watch_bar.add_child(_watch_btn)

	var unwatch_btn := Button.new()
	unwatch_btn.text = "Unwatch"
	unwatch_btn.pressed.connect(_on_unwatch_pressed)
	watch_bar.add_child(unwatch_btn)

	var rep_scroll := ScrollContainer.new()
	rep_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rep_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	rep_section.add_child(rep_scroll)

	_rep_grid = GridContainer.new()
	_rep_grid.columns = 1
	rep_scroll.add_child(_rep_grid)


func clear() -> void:
	_clear_grid(_vis_grid)
	_clear_grid(_rep_grid)
	_last_snapshot = {}
	_all_peers = []


# ─── Visibility Matrix ────────────────────────────────────────────────────────

func update_visibility_matrix(snapshot: Dictionary) -> void:
	_last_snapshot = snapshot
	_rebuild_visibility_matrix()


func _rebuild_visibility_matrix() -> void:
	_clear_grid(_vis_grid)

	var lobbies: Array = _last_snapshot.get("lobbies", [])
	if lobbies.is_empty():
		_vis_grid.columns = 1
		_vis_grid.add_child(_make_cell("No lobbies active", C_EMPTY, 200))
		return

	# Collect all peer IDs across all lobbies.
	var peer_set: Dictionary = {}
	for lobby in lobbies:
		for pid: int in lobby.get("connected_clients", []):
			peer_set[pid] = true
	_all_peers.clear()
	_all_peers.assign(peer_set.keys())
	_all_peers.sort()

	var col_count := _all_peers.size() + 1  # +1 for lobby name column
	_vis_grid.columns = col_count

	# Header row.
	_vis_grid.add_child(_make_header("Lobby \\ Peer"))
	for pid: int in _all_peers:
		_vis_grid.add_child(_make_header("P%d" % pid))

	# Data rows.
	for lobby in lobbies:
		var lobby_name: String = lobby.get("name", "?")
		var clients: Array = lobby.get("connected_clients", [])
		var process_mode: int = lobby.get("process_mode", 0)
		var frozen: bool = process_mode == Node.PROCESS_MODE_DISABLED

		_vis_grid.add_child(_make_label(lobby_name + (" ❄" if frozen else ""), 120))

		for pid: int in _all_peers:
			if pid == 1:  # server always visible
				_vis_grid.add_child(_make_cell("S", C_SERVER, 48))
			elif pid in clients:
				_vis_grid.add_child(_make_cell("✓", C_OK, 48))
			else:
				_vis_grid.add_child(_make_cell("✗", C_FAIL, 48))


# ─── Replication Matrix ───────────────────────────────────────────────────────

func update_replication_matrix(snapshot: Dictionary) -> void:
	_clear_grid(_rep_grid)

	var properties: Dictionary = snapshot.get("properties", {})
	var inventory: Array = snapshot.get("inventory", [])

	if properties.is_empty() and inventory.is_empty():
		_rep_grid.columns = 1
		_rep_grid.add_child(_make_cell("No data yet — click Watch", C_EMPTY, 260))
		return

	var peers := _all_peers
	var col_count := peers.size() + 1
	_rep_grid.columns = col_count

	# Synchronizer inventory header.
	if not inventory.is_empty():
		_rep_grid.columns = 3
		_rep_grid.add_child(_make_header("Synchronizer"))
		_rep_grid.add_child(_make_header("Authority"))
		_rep_grid.add_child(_make_header("Root Path"))
		for entry in inventory:
			_rep_grid.add_child(_make_label(entry.get("name", "?"), 120))
			_rep_grid.add_child(_make_label("P%d" % int(entry.get("authority", 0)), 60))
			_rep_grid.add_child(_make_label(str(entry.get("root_path", "")), 160))

		# Spacer.
		for i in 3:
			_rep_grid.add_child(_make_cell("", C_EMPTY, 10))

	# Property values table.
	if not properties.is_empty():
		_rep_grid.columns = 2
		_rep_grid.add_child(_make_header("Property"))
		_rep_grid.add_child(_make_header("Server Value"))
		for prop_path in properties:
			_rep_grid.add_child(_make_label(str(prop_path), 200))
			var val: Variant = properties[prop_path]
			_rep_grid.add_child(_make_label(_format_value(val), 140))


func _format_value(v: Variant) -> String:
	match typeof(v):
		TYPE_FLOAT:   return "%.3f" % v
		TYPE_VECTOR2: return "(%.2f, %.2f)" % [v.x, v.y]
		TYPE_VECTOR3: return "(%.2f, %.2f, %.2f)" % [v.x, v.y, v.z]
		_:            return str(v)


# ─── Watch Controls ───────────────────────────────────────────────────────────

func _on_watch_pressed() -> void:
	var path := _watch_path_edit.text.strip_edges()
	if path.is_empty() or not send_to_game.is_valid():
		return
	send_to_game.call("networked:watch_node", [{"node_path": path}])
	_watch_btn.text = "Watching…"


func _on_unwatch_pressed() -> void:
	var path := _watch_path_edit.text.strip_edges()
	if path.is_empty() or not send_to_game.is_valid():
		return
	send_to_game.call("networked:unwatch_node", [{"node_path": path}])
	_watch_btn.text = "Watch"
	_clear_grid(_rep_grid)


# ─── Cell Factories ───────────────────────────────────────────────────────────

func _make_header(text: String) -> Label:
	var lbl := Label.new()
	lbl.text = text
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.add_theme_font_size_override("font_size", 11)
	lbl.add_theme_color_override("font_color", Color(0.8, 0.8, 0.8))
	lbl.custom_minimum_size.x = 60
	return lbl


func _make_label(text: String, min_w: int) -> Label:
	var lbl := Label.new()
	lbl.text = text
	lbl.add_theme_font_size_override("font_size", 11)
	lbl.custom_minimum_size.x = min_w
	lbl.clip_text = true
	return lbl


func _make_cell(text: String, bg: Color, min_w: int) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(min_w, 24)

	var style := StyleBoxFlat.new()
	style.bg_color = bg
	style.set_border_width_all(0)
	style.set_corner_radius_all(2)
	panel.add_theme_stylebox_override("panel", style)

	var lbl := Label.new()
	lbl.text = text
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	lbl.add_theme_font_size_override("font_size", 11)
	panel.add_child(lbl)
	return panel


func _clear_grid(grid: GridContainer) -> void:
	for child in grid.get_children():
		child.queue_free()
	grid.columns = 1
