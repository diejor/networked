## Left pane of the Networked debugger: one row per registered [MultiplayerTree]
## peer, each with a right-aligned role/username chip ([TreeChips]), a dim
## online/offline status line, and a checkbox per debug panel.
##
## Owns only the tree. It reads peer data from the injected [DebuggerSession] and
## reports interaction up to [NetworkedDebuggerUI] through [signal panel_toggled]
## and [signal status_detail_requested]. Pin state is forwarded straight to
## [NetworkedDebuggerPlugin] since the tree already holds the session.
@tool
class_name PeerTreePanel
extends Tree

const TreeChips := preload("res://addons/networked/debug/editor/tree_chips.gd")

const PanelDataAdapter := preload("res://addons/networked/debug/editor/adapters/panel_data.gd")

## Emitted when a peer's panel checkbox is toggled.
signal panel_toggled(peer_key: String, panel_type: int, checked: bool)

## Emitted when a peer's panel status button is clicked, so the coordinator can
## surface the detail dialog.
signal status_detail_requested(title: String, level: int, summary: String)

## Emitted when the spawn ("+") button on a local app-tree row is clicked,
## asking the coordinator to clone that tree into its own window.
signal spawn_requested(peer_key: String)

## Emitted when the embed toggle (Debug icon) on a debugger-spawned row is
## clicked, asking the coordinator to flip that window embedded <-> native.
signal embed_toggle_requested(peer_key: String)

# TreeItem button ids for the local-row action buttons, distinct from the
# panel-status buttons ([method set_panel_status]) which use id 0.
const _SPAWN_BUTTON_ID := 7001
const _EMBED_BUTTON_ID := 7002

## Injected by [NetworkedDebuggerUI] before this node enters the scene tree.
var session: DebuggerSession

# Role chip glyph per [enum NetwMultiplayer.Role], drawn right-aligned on the
# peer row by [method _draw_peer_chips].
const _ROLE_GLYPH := {
	NetwMultiplayer.Role.NONE: "○ NONE",
	NetwMultiplayer.Role.CLIENT: "▸ CLIENT",
	NetwMultiplayer.Role.DEDICATED_SERVER: "▣ DEDICATED",
	NetwMultiplayer.Role.LISTEN_SERVER: "◈ LISTEN",
}

# [Dictionary] mapping [code]peer_key[/code] to its bold peer-root [TreeItem].
var _peer_tree_items: Dictionary[String, TreeItem] = { }

# [Dictionary] mapping [code]peer_key[/code] to its dim status-line child
# [TreeItem] (the [code]"● ONLINE · peer 1 · self"[/code] row).
var _status_items: Dictionary[String, TreeItem] = { }

# [Dictionary] mapping [code]adapter_key[/code] to its checkbox [TreeItem].
var _checkbox_items: Dictionary[String, TreeItem] = { }

# Right-aligned pill drawer for the peer rows (role + username chips).
var _chips := TreeChips.new(0.85)

# Set while applying a pin sync from the plugin so item_collapsed doesn't
# echo back into [method NetworkedDebuggerPlugin.set_peer_pinned].
var _syncing_pin: bool = false

var _dbg: NetwHandle = Netw.dbg.handle(self)
var _is_applying_theme: bool = false


func _init() -> void:
	custom_minimum_size.x = 260
	hide_root = true
	columns = 1
	set_column_title(0, "Multiplayer Tree")
	column_titles_visible = true
	set_column_expand(0, true)
	item_edited.connect(_on_item_edited)
	button_clicked.connect(_on_button_clicked)
	item_collapsed.connect(_on_item_collapsed)
	create_item() # ensure root exists


func _ready() -> void:
	_apply_theme()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_apply_theme()


# Editor-theme-aware backdrop; re-runs on theme change so light/dark updates live.
func _apply_theme() -> void:
	if not is_inside_tree() or _is_applying_theme:
		return
	_is_applying_theme = true
	var tree_bg := StyleBoxFlat.new()
	tree_bg.bg_color = get_theme_color("dark_color_2", "Editor")
	tree_bg.set_corner_radius_all(4)
	tree_bg.content_margin_left = 4
	tree_bg.content_margin_right = 4
	tree_bg.content_margin_top = 4
	tree_bg.content_margin_bottom = 4
	add_theme_stylebox_override("panel", tree_bg)
	_is_applying_theme = false

# ─── Public API (driven by the coordinator's session signals) ─────────────────


## Adds a peer row plus its status line and panel checkboxes.
func add_peer(
		peer_key: String,
		username: String,
		tree_name: String,
		role: NetwMultiplayer.Role,
		color: Color,
		is_remote: bool,
		peer_id: int,
) -> void:
	if peer_key in _peer_tree_items:
		remove_peer(peer_key)

	if not get_root():
		create_item()

	var peer_item := create_item(get_root())
	peer_item.set_metadata(
		0,
		{
			"is_remote": is_remote,
			"peer_key": peer_key,
			"role": role,
			"username": username,
			"color": color,
		},
	)
	peer_item.set_custom_color(0, color)
	peer_item.set_selectable(0, false)

	# CELL_MODE_CUSTOM still draws the text/color, then invokes the callback so
	# the chips overlay right-aligned. Trim the name if it would collide.
	peer_item.set_cell_mode(0, TreeItem.CELL_MODE_CUSTOM)
	peer_item.set_custom_draw_callback(0, _draw_peer_chips)
	peer_item.set_text_overrun_behavior(0, TextServer.OVERRUN_TRIM_ELLIPSIS)

	if peer_id != 0:
		peer_item.set_tooltip_text(0, "peer=%d" % peer_id)
	else:
		peer_item.set_tooltip_text(0, "peer=?")

	var font := get_theme_font(&"bold", &"Tree")
	if font:
		peer_item.set_custom_font(0, font)

	_peer_tree_items[peer_key] = peer_item

	# Local rows carry an action button: an app tree can be cloned ("+"); a
	# debugger-spawned tree instead toggles its window embedded <-> native.
	if not is_remote:
		var spawned: bool = false
		if session:
			spawned = session.get_peers().get(peer_key, { }).get("spawned", false)
		if spawned:
			peer_item.add_button(
				0,
				get_theme_icon("Debug", "EditorIcons"),
				_EMBED_BUTTON_ID,
			)
			peer_item.set_button_tooltip_text(
				0,
				peer_item.get_button_count(0) - 1,
				"Toggle this window between embedded and native",
			)
		else:
			peer_item.add_button(
				0,
				get_theme_icon("Add", "EditorIcons"),
				_SPAWN_BUTTON_ID,
			)
			peer_item.set_button_tooltip_text(
				0,
				peer_item.get_button_count(0) - 1,
				"Spawn a copy of this tree in its own window",
			)

	_add_status_line(peer_item, peer_key)
	_add_peer_panel_rows(peer_item, peer_key)

	_update_display(peer_key, tree_name, username, role)
	_refresh_status_line(peer_key)

	# Default collapsed (unpinned). Honor a prior pin decision if one exists.
	var already_pinned: bool = false
	var plugin: NetworkedDebuggerPlugin = session.plugin if session else null
	if plugin:
		already_pinned = plugin.is_peer_pinned(peer_key)
	_syncing_pin = true
	peer_item.set_collapsed(not already_pinned)
	_syncing_pin = false


## Refreshes a peer's username/role chips from the live registry after a rename
## or role transition.
func update_identity(peer_key: String, username: String) -> void:
	if peer_key not in _peer_tree_items or not session:
		return
	var info: Dictionary = session.get_peers().get(peer_key, { })
	var tree_name: String = info.get("tree_name", "")
	var role: NetwMultiplayer.Role = info.get("role", NetwMultiplayer.Role.NONE)
	_update_display(peer_key, tree_name, username, role)
	queue_redraw()
	_refresh_status_line(peer_key)


## Greys the panel checkboxes (not the status line) and refreshes the status text.
func set_online(peer_key: String, online: bool) -> void:
	if peer_key not in _peer_tree_items:
		return
	var child := _peer_tree_items[peer_key].get_first_child()
	while child:
		var cmeta: Variant = child.get_metadata(0)
		var is_status: bool = cmeta is Dictionary and cmeta.get("status_line", false)
		if not is_status:
			if not online:
				child.set_custom_color(0, Color(0.5, 0.5, 0.5))
			else:
				child.clear_custom_color(0)
		child = child.get_next()
	_refresh_status_line(peer_key)


func set_peer_id(peer_key: String, peer_id: int) -> void:
	if peer_key not in _peer_tree_items:
		return
	_peer_tree_items[peer_key].set_tooltip_text(0, "peer=%d" % peer_id)
	_refresh_status_line(peer_key)


## Updates a panel checkbox's warning/error button from its adapter status.
func set_panel_status(
		key: String,
		level: int,
		summary: String,
		panel_display: String,
) -> void:
	if key not in _checkbox_items or summary.is_empty():
		return
	var item: TreeItem = _checkbox_items[key]
	item.set_tooltip_text(0, summary)

	var button_idx := -1
	if item.get_button_count(0) > 0:
		button_idx = 0

	if level > 0:
		var icon_name := "NodeWarning" if level == 1 else "StatusError"
		var color_name := "warning_color" if level == 1 else "error_color"
		var icon := get_theme_icon(icon_name, "EditorIcons")
		var color := get_theme_color(color_name, "Editor")
		var type_str := "Warning" if level == 1 else "Error"
		if button_idx == -1:
			item.add_button(0, icon)
			button_idx = 0
		else:
			item.set_button(0, button_idx, icon)
		item.set_button_color(0, button_idx, color)
		item.set_button_tooltip_text(0, button_idx, "%s in %s:\n%s" % [type_str, panel_display, summary])
	elif button_idx != -1:
		item.erase_button(0, button_idx)


## Applies a pin state from the plugin without re-emitting item_collapsed back.
func sync_peer_expanded(peer_key: String, expanded: bool) -> void:
	if peer_key not in _peer_tree_items:
		return
	var item: TreeItem = _peer_tree_items[peer_key]
	var desired_collapsed: bool = not expanded
	if item.collapsed == desired_collapsed:
		return
	_syncing_pin = true
	item.set_collapsed(desired_collapsed)
	_syncing_pin = false


## Removes a peer row and its children (status line, checkboxes).
func remove_peer(peer_key: String) -> void:
	if peer_key not in _peer_tree_items:
		return
	var prefix := peer_key + ":"
	var to_remove: Array[String] = []
	for key in _checkbox_items:
		if key.begins_with(prefix):
			to_remove.append(key)
	for key in to_remove:
		_checkbox_items.erase(key)

	_peer_tree_items[peer_key].free()
	_peer_tree_items.erase(peer_key)
	_status_items.erase(peer_key)


## Wipes every row back to an empty root.
func clear_peers() -> void:
	_checkbox_items.clear()
	_peer_tree_items.clear()
	_status_items.clear()
	clear()
	create_item() # re-create invisible root

# ─── Tree event handlers ──────────────────────────────────────────────────────


func _on_item_edited() -> void:
	var item := get_edited()
	if not item:
		return
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var m: Dictionary = meta as Dictionary
	panel_toggled.emit(m.get("peer_key", ""), m.get("panel_type", 0), item.is_checked(0))


# Filters for peer-root items and forwards collapse as a pin request.
func _on_item_collapsed(item: TreeItem) -> void:
	if _syncing_pin or not item or not session or not session.plugin:
		return
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var pk: String = (meta as Dictionary).get("peer_key", "")
	if pk.is_empty():
		return
	(session.plugin as NetworkedDebuggerPlugin).set_peer_pinned(
		pk,
		not item.collapsed,
		session.session_id,
	)


func _on_button_clicked(item: TreeItem, _column: int, _id: int, _mb_idx: int) -> void:
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary or not session:
		return
	var m: Dictionary = meta as Dictionary
	var pk: String = m.get("peer_key", "")

	if _id == _SPAWN_BUTTON_ID:
		if not pk.is_empty():
			spawn_requested.emit(pk)
		return
	if _id == _EMBED_BUTTON_ID:
		if not pk.is_empty():
			embed_toggle_requested.emit(pk)
		return

	var pt: int = m.get("panel_type", 0)
	var key: String = "%s:%s" % [pk, PanelDataAdapter.PANEL_NAMES[pt]]

	var adapter: PanelDataAdapter = session.get_adapter(key)
	if not adapter:
		return
	var level: int = adapter.get_status_level()
	if level <= 0:
		return
	var peer_info: Dictionary = session.get_peers().get(pk, { })
	var peer_display: String = peer_info.get("display_name", pk)
	var title := "%s - %s" % [peer_display, PanelDataAdapter.PANEL_DISPLAY_NAMES[pt]]
	status_detail_requested.emit(title, level, adapter.get_status_banner_text())

# ─── Row construction / rendering ─────────────────────────────────────────────


func _add_peer_panel_rows(peer_item: TreeItem, peer_key: String) -> void:
	for pt in PanelDataAdapter.PANEL_NAMES.keys():
		_add_panel_checkbox(peer_item, peer_key, pt)


func _add_panel_checkbox(
		peer_item: TreeItem,
		peer_key: String,
		pt: PanelDataAdapter.PanelType,
) -> void:
	var child := create_item(peer_item)
	child.set_cell_mode(0, TreeItem.CELL_MODE_CHECK)
	child.set_text(0, PanelDataAdapter.PANEL_DISPLAY_NAMES[pt])
	child.set_editable(0, true)
	child.set_checked(0, false)
	child.set_metadata(0, { "peer_key": peer_key, "panel_type": pt })
	_checkbox_items["%s:%s" % [peer_key, PanelDataAdapter.PANEL_NAMES[pt]]] = child


# Creates the dim status-line child (first child, non-selectable, smaller font).
func _add_status_line(peer_item: TreeItem, peer_key: String) -> void:
	var child := create_item(peer_item)
	child.set_selectable(0, false)
	child.set_metadata(0, { "status_line": true })
	child.set_custom_color(0, Color(0.6, 0.6, 0.62))
	var fs := get_theme_font_size(&"font_size", &"Tree")
	if fs > 2:
		child.set_custom_font_size(0, fs - 2)
	_status_items[peer_key] = child


# Sets the row label to the tree name and stashes username/role on the item so
# [method _draw_peer_chips] can paint them as right-aligned chips.
func _update_display(
		peer_key: String,
		tree_name: String,
		username: String,
		role: NetwMultiplayer.Role,
) -> void:
	if peer_key not in _peer_tree_items:
		return
	var peer_item := _peer_tree_items[peer_key]
	peer_item.set_text(0, tree_name)
	var meta: Variant = peer_item.get_metadata(0)
	if meta is Dictionary:
		meta["username"] = username
		meta["role"] = role
		peer_item.set_metadata(0, meta)


func _refresh_status_line(peer_key: String) -> void:
	if peer_key not in _status_items or not session:
		return
	var info: Dictionary = session.get_peers().get(peer_key, { })
	var online: bool = info.get("online", false)
	var peer_id: int = info.get("peer_id", 0)
	var is_remote: bool = info.get("is_remote", false)
	var item: TreeItem = _status_items[peer_key]
	item.set_text(0, _status_line_text(online, peer_id, is_remote))
	# A string cell takes one color, so the whole line (dot included) tints by
	# status. CONNECTING (yellow) needs NetwMultiplayer.SessionState on the wire, which
	# the editor doesn't receive yet, so only ONLINE/OFFLINE are distinguished.
	item.set_custom_color(0, _status_color(online))


func _status_color(online: bool) -> Color:
	return Color(0.45, 0.80, 0.50) if online else Color(0.60, 0.60, 0.62)


func _status_line_text(online: bool, peer_id: int, is_remote: bool) -> String:
	var parts: Array[String] = []
	if online:
		parts.append("● ONLINE")
		if peer_id != 0:
			parts.append("peer %d" % peer_id)
		if not is_remote:
			parts.append("self")
	else:
		parts.append("○ OFFLINE")
		parts.append("not connected")
	return " · ".join(parts)


func _role_chip_color(role: int) -> Color:
	match role:
		NetwMultiplayer.Role.DEDICATED_SERVER:
			return Color(0.95, 0.65, 0.25)
		NetwMultiplayer.Role.LISTEN_SERVER:
			return Color(0.40, 0.82, 0.55)
		NetwMultiplayer.Role.CLIENT:
			return Color(0.45, 0.68, 0.95)
		_:
			return Color(0.60, 0.60, 0.62)


# Paints the peer row's right-aligned chips (username, then role flush-right).
# Delegates geometry to [TreeChips]; runs during the tree's draw pass.
func _draw_peer_chips(item: TreeItem, rect: Rect2) -> void:
	var meta: Variant = item.get_metadata(0)
	if not meta is Dictionary:
		return
	var role: int = meta.get("role", NetwMultiplayer.Role.NONE)
	var username: String = meta.get("username", "")
	var peer_color: Color = meta.get("color", Color.WHITE)

	var chips: Array[TreeChips.Chip] = []
	# Username has no meaning for a dedicated server (no local player there).
	if not username.is_empty() and role != NetwMultiplayer.Role.DEDICATED_SERVER:
		chips.append(TreeChips.Chip.new(username, peer_color))
	chips.append(TreeChips.Chip.new(_ROLE_GLYPH.get(role, "○ NONE"), _role_chip_color(role)))

	_chips.draw(item.get_tree(), rect, chips)
