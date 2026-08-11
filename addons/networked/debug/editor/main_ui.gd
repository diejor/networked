## Main [code]"Networked"[/code] tab in the editor debugger.
##
## A coordinator: it hosts the peer tree ([PeerTreePanel]) and the panel grid
## ([PanelGrid]) in an [HSplitContainer], fans the injected [DebuggerSession]'s
## signals out to both panes, and fans pane signals (checkbox toggles, status
## clicks) back. All tree rendering lives in [PeerTreePanel]; all panel lifecycle
## lives in [PanelGrid]. This class owns no per-peer or per-panel state.
@tool
class_name NetworkedDebuggerUI
extends VBoxContainer

const PanelDataAdapter := preload("res://addons/networked/debug/editor/adapters/panel_data.gd")

## Injected by [NetworkedDebuggerPlugin] before the node enters the scene tree.
var session: DebuggerSession

var _split: HSplitContainer
var _tree: PeerTreePanel
var _grid: PanelGrid


func _ready() -> void:
	custom_minimum_size.y = 250
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	_split = HSplitContainer.new()
	_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_split.split_offset = 260
	add_child(_split)

	_tree = PeerTreePanel.new()
	_tree.session = session
	_split.add_child(_tree)

	_grid = PanelGrid.new()
	_grid.session = session
	_split.add_child(_grid)

	_tree.panel_toggled.connect(_on_panel_toggled)
	_tree.status_detail_requested.connect(_show_status_detail)
	_grid.status_detail_requested.connect(_show_status_detail)

	if not session:
		return

	_tree.spawn_requested.connect(session.send_spawn_tree)
	_tree.embed_toggle_requested.connect(session.send_toggle_embed)

	session.peer_registered.connect(_on_peer_registered)
	session.peer_unregistered.connect(_on_peer_unregistered)
	session.peer_status_changed.connect(_on_peer_status_changed)
	session.peer_identity_changed.connect(_on_peer_identity_changed)
	session.peer_id_resolved.connect(_tree.set_peer_id)
	session.adapter_data_changed.connect(_on_adapter_data_changed)
	session.session_cleared.connect(_on_session_cleared)

# ─── Session → panes ──────────────────────────────────────────────────────────


func _on_peer_registered(
		peer_key: String,
		display_name: String,
		tree_name: String,
		role: NetwMultiplayer.Role,
		color: Color,
		is_remote: bool,
		peer_id: int,
) -> void:
	_tree.add_peer(peer_key, display_name, tree_name, role, color, is_remote, peer_id)


func _on_peer_unregistered(peer_key: String) -> void:
	_grid.deactivate_peer(peer_key)
	_tree.remove_peer(peer_key)


func _on_peer_status_changed(peer_key: String, online: bool) -> void:
	_tree.set_online(peer_key, online)
	_grid.set_peer_online(peer_key, online)


func _on_peer_identity_changed(peer_key: String, username: String) -> void:
	_tree.update_identity(peer_key, username)


func _on_adapter_data_changed(key: String) -> void:
	_refresh_tree_status(key)
	_grid.apply_adapter_data(key)


func _on_session_cleared() -> void:
	_tree.clear_peers()
	_grid.clear_panels()

# ─── Panes → coordinator ──────────────────────────────────────────────────────


func _on_panel_toggled(peer_key: String, panel_type: int, checked: bool) -> void:
	_grid.set_panel_active(peer_key, panel_type, checked)
	if checked:
		_refresh_tree_status("%s:%s" % [peer_key, PanelDataAdapter.PANEL_NAMES[panel_type]])


# Pushes an adapter's current status onto its peer-tree checkbox.
func _refresh_tree_status(key: String) -> void:
	if not session:
		return
	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		return
	_tree.set_panel_status(
		key,
		adapter.get_status_level(),
		adapter.get_status_banner_text(),
		PanelDataAdapter.PANEL_DISPLAY_NAMES[adapter.panel_type],
	)


func _show_status_detail(title: String, level: int, summary: String) -> void:
	var dialog := AcceptDialog.new()
	var type_str := "Warning" if level == 1 else "Error"
	dialog.title = "%s has a %s!" % [title, type_str]

	var rtl := RichTextLabel.new()
	rtl.selection_enabled = true
	rtl.bbcode_enabled = true
	rtl.fit_content = true
	rtl.custom_minimum_size = Vector2(400, 100)

	var bullet_text := ""
	for line in summary.split("|"):
		bullet_text += "[color=gray]*[/color] " + line.strip_edges() + "\n"

	rtl.text = bullet_text
	dialog.add_child(rtl)
	add_child(dialog)
	dialog.popup_centered()
	dialog.visibility_changed.connect(
		func():
			if not dialog.visible:
				dialog.queue_free()
	)

# ─── Plugin-facing (names preserved) ──────────────────────────────────────────


## Applies a pin state from the plugin without echoing it back.
func sync_peer_expanded(peer_key: String, expanded: bool) -> void:
	_tree.sync_peer_expanded(peer_key, expanded)


## Called by [NetworkedDebuggerPlugin._breakpoint_set_in_tree].
func on_breakpoint_changed(source: String, line: int, enabled: bool) -> void:
	_grid.on_breakpoint_changed(source, line, enabled)


## Called by [NetworkedDebuggerPlugin._breakpoints_cleared_in_tree].
func on_breakpoints_cleared() -> void:
	_grid.on_breakpoints_cleared()
