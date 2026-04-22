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
##   synchronizer tree   — Name / Mode / Flags / Source columns
@tool
class_name PanelTopology
extends DebugPanel

## Called when the user clicks the node path button (local peers only).
## Receives the raw [code]node_path[/code] string from the snapshot.
var on_node_inspect: Callable

## Called when the user clicks the refresh button.
var on_refresh_requested: Callable

## Called when the Nameplate visualizer is toggled.
## Signature: func(node_path: String, enabled: bool) -> void
var on_nameplate_toggled: Callable

# ─── Widgets ──────────────────────────────────────────────────────────────────

var _username_label: Label
var _peer_id_label: Label
var _lobby_label: Label
var _mode_label: Label
var _node_btn: Button

var _nameplate_btn: CheckButton

var _sync_tree: Tree

# ─── State ────────────────────────────────────────────────────────────────────

var _stale_since_usec: int = -1
var _last_node_path: String = ""

# Replication mode int → short string
const _MODE_LABELS: Dictionary = {
	0: "Never",
	1: "Always",
	2: "On change",
}


func _ready() -> void:
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	_build_layout()


# ─── Panel interface ──────────────────────────────────────────────────────────

func clear() -> void:
	_last_node_path = ""
	_stale_since_usec = -1
	if _username_label:
		_username_label.text = "—"
		_peer_id_label.text = "—"
		_lobby_label.text = "—"
		_mode_label.text = "—"
	if _node_btn:
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


func on_new_entry(entry: Variant) -> void:
	var d: Dictionary = entry as Dictionary
	_apply_identity(d)
	_populate_sync_tree(d)
	_update_cache_visuals(d.get("cache_info", {}))


func _update_cache_visuals(cache_info: Dictionary) -> void:
	var hit: bool = cache_info.get("hit", false)
	var hooked: bool = cache_info.get("hooked", false)
	
	var status_text := "cached" if hit else "searched"
	if not hooked:
		status_text = "not hooked"
	
	_sync_tree.set_column_title(0, "Synchronizers (%s)" % status_text)
	
	var status_color: Color = get_theme_color("success_color", "Editor")
	if not hooked:
		status_color = get_theme_color("error_color", "Editor")
	elif not hit:
		status_color = get_theme_color("warning_color", "Editor")
		
	_sync_tree.add_theme_color_override("font_title_color", status_color)


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
	if _node_btn:
		_node_btn.disabled = node_path.is_empty() or not on_node_inspect.is_valid()


# ─── Synchronizer tree ────────────────────────────────────────────────────────

func _populate_sync_tree(d: Dictionary) -> void:
	_sync_tree.clear()
	_sync_tree.create_item()  # invisible root

	var cache_info: Dictionary = d.get("cache_info", {})
	var hit: bool = cache_info.get("hit", false)
	var hooked: bool = cache_info.get("hooked", false)

	var sync_icon := get_theme_icon("MultiplayerSynchronizer", "EditorIcons")
	var cache_icon := get_theme_icon("InstanceOptions", "EditorIcons")

	var status_color: Color = get_theme_color("success_color", "Editor")
	if not hooked:
		status_color = get_theme_color("error_color", "Editor")
	elif not hit:
		status_color = get_theme_color("warning_color", "Editor")

	for sd: Dictionary in d.get("synchronizers", []):
		var sync_item := _sync_tree.create_item(_sync_tree.get_root())
		var auth_peer: int = sd.get("authority", 0)
		var is_srv := auth_peer == 1
		var auth_tag: String = " [server]" if is_srv else " [client]"

		sync_item.set_text(0, sd.get("name", "?") + auth_tag)
		sync_item.set_icon(0, sync_icon)

		sync_item.set_selectable(0, false)
		sync_item.set_selectable(1, false)

		for pd: Dictionary in sd.get("properties", []):
			var prop_item := _sync_tree.create_item(sync_item)
			var mode_int: int = pd.get("replication_mode", 0)
			var mode_str: String = _MODE_LABELS.get(mode_int, str(mode_int))
			
			prop_item.set_text(0, pd.get("path", "?"))
			prop_item.set_text(1, mode_str)
			
			# Property Class/Type Icon (e.g. String, int, Vector2, or specific Resource)
			var type_name: String = pd.get("target_class", "")
			if not type_name.is_empty() and has_theme_icon(type_name, "EditorIcons"):
				prop_item.set_icon(0, get_theme_icon(type_name, "EditorIcons"))
			elif pd.get("type", 0) != TYPE_NIL:
				var fallback_type := type_string(pd.get("type", 0))
				if has_theme_icon(fallback_type, "EditorIcons"):
					prop_item.set_icon(0, get_theme_icon(fallback_type, "EditorIcons"))
			
			prop_item.set_selectable(0, false)
			prop_item.set_selectable(1, false)


# ─── Layout construction ──────────────────────────────────────────────────────

func _build_layout() -> void:
	add_theme_constant_override("separation", 6)

	_build_identity_section()
	_build_sync_tree()


func _build_identity_section() -> void:
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = get_theme_color("dark_color_1", "Editor")
	style.set_border_width_all(0)
	style.content_margin_left = 8
	style.content_margin_right = 8
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)
	panel.add_child(vbox)

	var row1 := HBoxContainer.new()
	row1.add_theme_constant_override("separation", 12)
	vbox.add_child(row1)

	var un_lbl := Label.new(); un_lbl.text = "Username:"
	un_lbl.add_theme_color_override("font_color", get_theme_color("font_disabled_color", "Editor"))
	row1.add_child(un_lbl)
	_username_label = Label.new(); _username_label.text = "—"
	_username_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_username_label.add_theme_font_size_override("font_size", 14)
	row1.add_child(_username_label)

	var pid_lbl := Label.new(); pid_lbl.text = "Peer ID:"
	pid_lbl.add_theme_color_override("font_color", get_theme_color("font_disabled_color", "Editor"))
	row1.add_child(pid_lbl)
	_peer_id_label = Label.new(); _peer_id_label.text = "—"
	row1.add_child(_peer_id_label)

	var row2 := HBoxContainer.new()
	row2.add_theme_constant_override("separation", 12)
	vbox.add_child(row2)

	var lb_lbl := Label.new(); lb_lbl.text = "Lobby:"
	lb_lbl.add_theme_color_override("font_color", get_theme_color("font_disabled_color", "Editor"))
	row2.add_child(lb_lbl)
	_lobby_label = Label.new(); _lobby_label.text = "—"
	_lobby_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row2.add_child(_lobby_label)

	var mode_hdr := Label.new(); mode_hdr.text = "Mode:"
	mode_hdr.add_theme_color_override("font_color", get_theme_color("font_disabled_color", "Editor"))
	row2.add_child(mode_hdr)
	_mode_label = Label.new(); _mode_label.text = "—"
	_mode_label.add_theme_color_override("font_color", get_theme_color("warning_color", "Editor"))
	row2.add_child(_mode_label)

	var row3 := HBoxContainer.new()
	row3.add_theme_constant_override("separation", 8)
	vbox.add_child(row3)

	_node_btn = Button.new()
	_node_btn.text = "Inspect Node"
	_node_btn.tooltip_text = "Select this node in the Remote Scene Tree."
	_node_btn.disabled = true
	_node_btn.pressed.connect(func() -> void:
		if on_node_inspect.is_valid() and not _last_node_path.is_empty():
			on_node_inspect.call(_last_node_path)
	)
	row3.add_child(_node_btn)

	_nameplate_btn = CheckButton.new()
	_nameplate_btn.text = "Nameplate"
	_nameplate_btn.tooltip_text = "Toggle the in-world nameplate for this player."
	_nameplate_btn.mouse_filter = Control.MOUSE_FILTER_STOP
	_nameplate_btn.toggled.connect(func(pressed: bool) -> void:
		if on_nameplate_toggled.is_valid() and not _last_node_path.is_empty():
			on_nameplate_toggled.call(_last_node_path, pressed)
	)
	row3.add_child(_nameplate_btn)


func _build_sync_tree() -> void:
	_sync_tree = Tree.new()
	_sync_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_sync_tree.hide_root = true
	_sync_tree.columns = 2
	_sync_tree.set_column_title(0, "Synchronizers (cached)")
	_sync_tree.set_column_title(1, "Mode")
	_sync_tree.column_titles_visible = true
	_sync_tree.set_column_expand(0, true)
	_sync_tree.set_column_expand(1, false)
	_sync_tree.set_column_custom_minimum_width(1, 120)
	_sync_tree.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	_sync_tree.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_sync_tree.add_theme_stylebox_override("selected", StyleBoxEmpty.new())
	_sync_tree.add_theme_stylebox_override("selected_focus", StyleBoxEmpty.new())
	_sync_tree.add_theme_stylebox_override("cursor", StyleBoxEmpty.new())
	_sync_tree.add_theme_stylebox_override("cursor_unfocused", StyleBoxEmpty.new())
	_sync_tree.create_item()  # invisible root
	add_child(_sync_tree)
