## Topology panel — shows synchronizer tree and identity for one player peer.
##
## Displays the latest [NetTopologySnapshot] received from the reporter.
## Topology is current-state (not time-series), so [method populate] shows only
## the most recent entry and [method on_new_entry] replaces rather than appends.
##
## Layout (top to bottom):
##   [cache banner]      — hidden unless snapshot is stale
##   identity rows       — username / peer_id, lobby / mode
##   node button         — opens the node in the editor scene inspector
##   visualizer toolbar  — toggle overlays on the game side
##   synchronizer tree   — Name / Mode / Flags / Source columns
@tool
class_name PanelTopology
extends DebugPanel

## Called when the user clicks the node path button (local peers only).
## Receives the raw [code]node_path[/code] string from the snapshot.
var on_node_inspect: Callable

## Called when a visualizer toggle button changes state.
## Receives [code](viz_name: String, enabled: bool)[/code].
var on_visualizer_toggle: Callable

# ─── Widgets ──────────────────────────────────────────────────────────────────

var _cache_banner: PanelContainer
var _cache_label: Label
var _refresh_btn: Button

var _username_label: Label
var _peer_id_label: Label
var _lobby_label: Label
var _mode_label: Label
var _node_btn: Button

var _nameplate_btn: CheckButton
var _bounds_btn: CheckButton
var _colliders_btn: CheckButton

var _sync_tree: Tree

# ─── State ────────────────────────────────────────────────────────────────────

var _stale_since_usec: int = -1
var _last_node_path: String = ""

# Replication mode int → short string
const _MODE_LABELS: Dictionary = {
	0: "NEVER",   # SceneReplicationConfig.REPLICATION_MODE_NEVER
	1: "ALWAYS",  # SceneReplicationConfig.REPLICATION_MODE_ALWAYS
	2: "ON_CHANGE",
}


func _ready() -> void:
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	_build_layout()


func _process(_delta: float) -> void:
	if _stale_since_usec >= 0 and is_instance_valid(_cache_label):
		var elapsed := int((Time.get_ticks_usec() - _stale_since_usec) / 1_000_000)
		_cache_label.text = "Cache: STALE  (%ds ago)" % elapsed


# ─── Panel interface ──────────────────────────────────────────────────────────

func clear() -> void:
	_last_node_path = ""
	_stale_since_usec = -1
	if _cache_banner:
		_cache_banner.hide()
	if _username_label:
		_username_label.text = "—"
		_peer_id_label.text = "—"
		_lobby_label.text = "—"
		_mode_label.text = "—"
	if _node_btn:
		_node_btn.text = ""
		_node_btn.disabled = true
	if _sync_tree:
		_sync_tree.clear()
		_sync_tree.create_item()


## Shows only the latest entry — topology is current-state, not time-series.
func populate(buffer: Array) -> void:
	clear()
	if not buffer.is_empty():
		on_new_entry(buffer[-1])


## Called by [PanelWrapper.set_online] when the owning tree goes offline or comes back.
## Disables interactive controls so the user can't send RPCs to a dead tree.
func set_peer_online(online: bool) -> void:
	if _node_btn:
		_node_btn.disabled = not online or _last_node_path.is_empty() or not on_node_inspect.is_valid()
	if _nameplate_btn:
		_nameplate_btn.disabled = not online
	if _bounds_btn:
		_bounds_btn.disabled = not online
	if _colliders_btn:
		_colliders_btn.disabled = not online


func on_new_entry(entry: Variant) -> void:
	var d: Dictionary = entry as Dictionary
	_apply_identity(d)
	_populate_sync_tree(d)


# ─── Identity rows ────────────────────────────────────────────────────────────

func _apply_identity(d: Dictionary) -> void:
	var node_path: String = d.get("node_path", "")
	var peer_id: int = d.get("peer_id", 0)
	var lobby: String = d.get("lobby_name", "—")
	var is_server: bool = d.get("is_server", false)
	var mode_str: String = "SERVER" if is_server else "CLIENT"

	var username: String = d.get("username", "—")

	_username_label.text = username
	_peer_id_label.text = str(peer_id) if peer_id != 0 else "—"
	_lobby_label.text = lobby
	_mode_label.text = "[%s]" % mode_str

	_last_node_path = node_path
	_node_btn.text = node_path if not node_path.is_empty() else "(unknown)"
	_node_btn.disabled = node_path.is_empty() or not on_node_inspect.is_valid()


# ─── Synchronizer tree ────────────────────────────────────────────────────────

func _populate_sync_tree(d: Dictionary) -> void:
	_sync_tree.clear()
	_sync_tree.create_item()  # invisible root

	for sd: Dictionary in d.get("synchronizers", []):
		var sync_item := _sync_tree.create_item(_sync_tree.get_root())
		var auth_peer: int = sd.get("authority", 0)
		var auth_str: String = "auth=server" if auth_peer == 1 else "auth=client"
		sync_item.set_text(0, "▼ " + sd.get("name", "?"))
		sync_item.set_text(1, "root=%s" % sd.get("root_path", "."))
		sync_item.set_text(2, "✓" if sd.get("enabled", true) else "✗")
		sync_item.set_text(3, auth_str)
		sync_item.set_selectable(0, false)
		sync_item.set_selectable(1, false)
		sync_item.set_selectable(2, false)
		sync_item.set_selectable(3, false)

		for pd: Dictionary in sd.get("properties", []):
			var prop_item := _sync_tree.create_item(sync_item)
			var mode_int: int = pd.get("replication_mode", 0)
			var mode_str: String = _MODE_LABELS.get(mode_int, str(mode_int))
			var w: String = "w✓" if pd.get("spawn", false) else "w✗"
			var s: String = "s✓" if pd.get("sync", false) else "s✗"
			var src: String = pd.get("source_path", "")
			prop_item.set_text(0, "  " + pd.get("path", "?"))
			prop_item.set_text(1, mode_str)
			prop_item.set_text(2, "%s %s" % [w, s])
			prop_item.set_text(3, "← %s" % src if not src.is_empty() else "")
			prop_item.set_selectable(0, false)
			prop_item.set_selectable(1, false)
			prop_item.set_selectable(2, false)
			prop_item.set_selectable(3, false)


# ─── Layout construction ──────────────────────────────────────────────────────

func _build_layout() -> void:
	add_theme_constant_override("separation", 4)

	_build_cache_banner()
	_build_identity_rows()
	_build_node_button()
	_build_visualizer_toolbar()
	_build_sync_tree()


func _build_cache_banner() -> void:
	_cache_banner = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.6, 0.4, 0.1, 0.25)
	style.set_corner_radius_all(3)
	style.content_margin_left = 6
	style.content_margin_right = 6
	style.content_margin_top = 3
	style.content_margin_bottom = 3
	_cache_banner.add_theme_stylebox_override("panel", style)
	_cache_banner.hide()
	add_child(_cache_banner)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	_cache_banner.add_child(row)

	_cache_label = Label.new()
	_cache_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cache_label.add_theme_color_override("font_color", Color(1.0, 0.75, 0.3))
	row.add_child(_cache_label)

	_refresh_btn = Button.new()
	_refresh_btn.text = "Refresh"
	_refresh_btn.flat = true
	row.add_child(_refresh_btn)


func _build_identity_rows() -> void:
	var row1 := HBoxContainer.new()
	row1.add_theme_constant_override("separation", 12)
	add_child(row1)

	var un_lbl := Label.new(); un_lbl.text = "Username:"
	un_lbl.add_theme_color_override("font_color", Color(0.6, 0.6, 0.6))
	row1.add_child(un_lbl)
	_username_label = Label.new(); _username_label.text = "—"
	_username_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row1.add_child(_username_label)

	var pid_lbl := Label.new(); pid_lbl.text = "Peer ID:"
	pid_lbl.add_theme_color_override("font_color", Color(0.6, 0.6, 0.6))
	row1.add_child(pid_lbl)
	_peer_id_label = Label.new(); _peer_id_label.text = "—"
	row1.add_child(_peer_id_label)

	var row2 := HBoxContainer.new()
	row2.add_theme_constant_override("separation", 12)
	add_child(row2)

	var lb_lbl := Label.new(); lb_lbl.text = "Lobby:"
	lb_lbl.add_theme_color_override("font_color", Color(0.6, 0.6, 0.6))
	row2.add_child(lb_lbl)
	_lobby_label = Label.new(); _lobby_label.text = "—"
	_lobby_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row2.add_child(_lobby_label)

	var mode_hdr := Label.new(); mode_hdr.text = "Mode:"
	mode_hdr.add_theme_color_override("font_color", Color(0.6, 0.6, 0.6))
	row2.add_child(mode_hdr)
	_mode_label = Label.new(); _mode_label.text = "—"
	row2.add_child(_mode_label)


func _build_node_button() -> void:
	_node_btn = Button.new()
	_node_btn.text = ""
	_node_btn.disabled = true
	_node_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_node_btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
	_node_btn.flat = false
	_node_btn.pressed.connect(func() -> void:
		if on_node_inspect.is_valid() and not _last_node_path.is_empty():
			on_node_inspect.call(_last_node_path)
	)
	add_child(_node_btn)


func _build_visualizer_toolbar() -> void:
	var toolbar := HBoxContainer.new()
	toolbar.add_theme_constant_override("separation", 6)
	add_child(toolbar)

	_nameplate_btn = _make_viz_toggle("Nameplate", toolbar)
	_bounds_btn    = _make_viz_toggle("TP bounds", toolbar)
	_colliders_btn = _make_viz_toggle("Colliders", toolbar)


func _make_viz_toggle(label: String, parent: Node) -> CheckButton:
	var btn := CheckButton.new()
	btn.text = label
	btn.toggled.connect(func(pressed: bool) -> void:
		if on_visualizer_toggle.is_valid():
			on_visualizer_toggle.call(label.to_lower().replace(" ", "_"), pressed)
	)
	parent.add_child(btn)
	return btn


func _build_sync_tree() -> void:
	_sync_tree = Tree.new()
	_sync_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_sync_tree.hide_root = true
	_sync_tree.columns = 4
	_sync_tree.set_column_title(0, "Name")
	_sync_tree.set_column_title(1, "Mode")
	_sync_tree.set_column_title(2, "Flags")
	_sync_tree.set_column_title(3, "Source")
	_sync_tree.column_titles_visible = true
	_sync_tree.set_column_expand(0, true)
	_sync_tree.set_column_expand(1, false)
	_sync_tree.set_column_expand(2, false)
	_sync_tree.set_column_expand(3, true)
	_sync_tree.set_column_custom_minimum_width(1, 80)
	_sync_tree.set_column_custom_minimum_width(2, 60)
	_sync_tree.create_item()  # invisible root
	add_child(_sync_tree)
