## Reference UI for [ConnectSession]. Renders the live target list,
## offers Add / Host / Refresh buttons, and routes user input through
## the merged [ConnectPopup].
##
## The app may inject its own [member session] via the export; if
## none is supplied, the browser auto-creates one as a child, binds
## [member tree_path] to it, and loads the persisted server list at
## [member server_list_path]. Custom UIs are expected to drive
## [ConnectSession] directly without touching this scene.
class_name ConnectBrowser
extends Control


const _ROW_SCENE := preload(
	"res://addons/networked/connect/ui/connect_browser_row.tscn"
)
const _POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/connect_popup.tscn"
)

const _ROW_MENU_JOIN := 1
const _ROW_MENU_EDIT := 2
const _ROW_MENU_REMOVE := 3


## Pre-built [ConnectSession] supplied by the app. When [code]null[/code]
## the browser instantiates and parents one itself; the
## auto-created session also binds [member tree_path] and loads
## [member server_list_path].
@export var session: ConnectSession:
	set(value):
		if _session == value:
			return
		_unbind_session_signals()
		_session = value
		if is_inside_tree():
			_bind_session_signals()
			_rebuild_from_session()
			_clear_selection()
	get:
		return _session

## Backends offered by the Add Server and Host popups.
@export var backend_templates: Array[BackendPeer] = []

## Spawner picker choices shown in the Host / Join popup.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:SpawnerComponent",
)
var spawner_options: Array[SceneNodePath] = []

## Path to the [MultiplayerTree] used for host / join. Only consulted
## when the browser auto-creates its [member session].
@export var tree_path: NodePath

## When [code]true[/code], hides this browser on
## [signal ConnectSession.session_entered] and shows it again on
## [signal ConnectSession.session_left]. UI policy only.
@export var hide_when_session_active: bool = true

## Path used when auto-creating the [member session] for persistence
## of direct targets.
@export var server_list_path: String = ServerList.DEFAULT_PATH


var _popup: ConnectPopup
var _row_menu: PopupMenu
var _session: ConnectSession
var _rows: Dictionary = {}  # JoinTarget -> ConnectBrowserRow
var _selected_row: ConnectBrowserRow
var _last_username: String = "Player"


@onready var _count_label: Label = %CountLabel
@onready var _refresh_button: Button = %RefreshButton
@onready var _add_button: Button = %AddButton
@onready var _host_button: Button = %HostButton
@onready var _list_box: VBoxContainer = %ListBox
@onready var _empty_state: VBoxContainer = %EmptyState
@onready var _details_label: Label = %DetailsLabel
@onready var _banner: HBoxContainer = %Banner
@onready var _banner_label: Label = %BannerLabel


func _ready() -> void:
	if session == null:
		session = ConnectSession.new()
		add_child(session)
		if not tree_path.is_empty():
			var tree := get_node_or_null(tree_path) as MultiplayerTree
			if tree:
				session.bind_tree(tree)
		session.load_server_list(server_list_path)

	_popup = _POPUP_SCENE.instantiate()
	add_child(_popup)
	_popup.set_templates(backend_templates)
	_popup.set_spawner_options(spawner_options)
	_popup.target_submitted.connect(_on_target_submitted)
	_popup.host_submitted.connect(_on_host_submitted)
	_popup.join_submitted.connect(_on_join_submitted)

	_row_menu = PopupMenu.new()
	add_child(_row_menu)
	_row_menu.id_pressed.connect(_on_row_menu_id_pressed)

	_refresh_button.pressed.connect(_on_refresh_pressed)
	_add_button.pressed.connect(_on_add_pressed)
	_host_button.pressed.connect(_on_host_pressed)

	_bind_session_signals()

	_rebuild_from_session()
	_clear_selection()
	session.refresh()


func _exit_tree() -> void:
	_unbind_session_signals()


func _bind_session_signals() -> void:
	if session == null:
		return
	if not session.target_added.is_connected(_on_target_added):
		session.target_added.connect(_on_target_added)
	if not session.target_removed.is_connected(_on_target_removed):
		session.target_removed.connect(_on_target_removed)
	if not session.target_updated.is_connected(_on_target_updated):
		session.target_updated.connect(_on_target_updated)
	if not session.session_entered.is_connected(_on_session_entered):
		session.session_entered.connect(_on_session_entered)
	if not session.session_left.is_connected(_on_session_left):
		session.session_left.connect(_on_session_left)
	if not session.host_failed.is_connected(_show_banner):
		session.host_failed.connect(_show_banner)
	if not session.join_failed.is_connected(_on_join_failed):
		session.join_failed.connect(_on_join_failed)
	if not session.provider_unavailable.is_connected(
		_on_provider_unavailable
	):
		session.provider_unavailable.connect(_on_provider_unavailable)


func _unbind_session_signals() -> void:
	if session == null:
		return
	if session.target_added.is_connected(_on_target_added):
		session.target_added.disconnect(_on_target_added)
	if session.target_removed.is_connected(_on_target_removed):
		session.target_removed.disconnect(_on_target_removed)
	if session.target_updated.is_connected(_on_target_updated):
		session.target_updated.disconnect(_on_target_updated)
	if session.session_entered.is_connected(_on_session_entered):
		session.session_entered.disconnect(_on_session_entered)
	if session.session_left.is_connected(_on_session_left):
		session.session_left.disconnect(_on_session_left)
	if session.host_failed.is_connected(_show_banner):
		session.host_failed.disconnect(_show_banner)
	if session.join_failed.is_connected(_on_join_failed):
		session.join_failed.disconnect(_on_join_failed)
	if session.provider_unavailable.is_connected(_on_provider_unavailable):
		session.provider_unavailable.disconnect(_on_provider_unavailable)


func _rebuild_from_session() -> void:
	for child in _list_box.get_children():
		child.queue_free()
	_rows.clear()
	if session == null:
		_update_counter()
		return
	for target in session.get_targets():
		_add_row(target)
	_update_counter()


func _add_row(target: JoinTarget) -> void:
	var row := _ROW_SCENE.instantiate() as ConnectBrowserRow
	_list_box.add_child(row)
	row.bind_target(target)
	var existing := session.get_result(target)
	if existing != null:
		row.set_result(existing)
	row.selected.connect(_on_row_selected.bind(row))
	row.context_requested.connect(_on_row_context_requested)
	row.activated.connect(_on_row_activated)
	_rows[target] = row


func _on_target_added(target: JoinTarget) -> void:
	if _rows.has(target):
		return
	_add_row(target)
	_update_counter()


func _on_target_removed(target: JoinTarget) -> void:
	var row: ConnectBrowserRow = _rows.get(target)
	if row != null:
		row.queue_free()
		_rows.erase(target)
	if _selected_row == row:
		_clear_selection()
	_update_counter()


func _on_target_updated(target: JoinTarget, result: ServerInfoResult) -> void:
	var row: ConnectBrowserRow = _rows.get(target)
	if row != null:
		row.set_result(result)
	if _selected_row != null and _selected_row.target == target:
		_update_details()


func _update_counter() -> void:
	var total := _rows.size()
	_count_label.text = str(total)
	_empty_state.visible = total == 0


func _on_row_selected(_target: JoinTarget, row: ConnectBrowserRow) -> void:
	if _selected_row and is_instance_valid(_selected_row):
		_selected_row.button_pressed = false
	_selected_row = row
	_update_details()


func _clear_selection() -> void:
	_selected_row = null
	_update_details()


func _update_details() -> void:
	if _selected_row == null or _selected_row.target == null:
		_details_label.text = "Select a server to see details"
		return
	var t := _selected_row.target
	var r := _selected_row.result
	var lines: PackedStringArray = []
	if t.is_direct():
		lines.append("Address: %s" % ConnectUiShared.format_address(t))
		lines.append(
			"Backend: %s" % ConnectUiShared.format_backend_label(t.backend)
		)
		lines.append("Status: %s" % _status_text(r))
		lines.append(
			"Latency: %s" % (
				"%d ms" % r.latency_ms if r and r.is_ok() else "-"
			)
		)
		lines.append("Players: %s" % _players_text(r))
		lines.append("Saved: Yes")
	else:
		lines.append("Provider: %s" % String(t.provider_id).capitalize())
		lines.append("Lobby ID: %s" % str(t.remote_id))
		lines.append("Status: %s" % _status_text(r))
		lines.append("Players: %s" % _players_text(r))
		lines.append("Saved: No - ephemeral")
	_details_label.text = "\n".join(lines)


func _on_row_context_requested(
	_target: JoinTarget,
	row: ConnectBrowserRow,
	screen_position: Vector2,
) -> void:
	if row == null or row.target == null:
		return
	_on_row_selected(row.target, row)
	row.button_pressed = true
	_row_menu.clear()
	_row_menu.add_item("Join", _ROW_MENU_JOIN)
	if row.target.is_direct():
		_row_menu.add_separator()
		_row_menu.add_item("Edit", _ROW_MENU_EDIT)
		_row_menu.add_item("Remove", _ROW_MENU_REMOVE)
	_row_menu.popup(Rect2i(Vector2i(screen_position), Vector2i.ZERO))


func _on_row_menu_id_pressed(id: int) -> void:
	match id:
		_ROW_MENU_JOIN:
			_open_join_for_selected()
		_ROW_MENU_EDIT:
			_open_edit_for_selected()
		_ROW_MENU_REMOVE:
			_remove_selected()


func _on_row_activated(_target: JoinTarget, row: ConnectBrowserRow) -> void:
	if row == null or row.target == null:
		return
	_on_row_selected(row.target, row)
	row.button_pressed = true
	_open_join_for_selected()


func _on_add_pressed() -> void:
	_popup.set_templates(backend_templates)
	_popup.open_add()


func _on_refresh_pressed() -> void:
	if session != null:
		session.refresh()


func _on_host_pressed() -> void:
	if session == null:
		return
	_popup.set_templates(backend_templates)
	_popup.set_spawner_options(spawner_options)
	_popup.open_host(
		backend_templates,
		session.get_provider_ids(),
		_last_username,
	)


func _open_join_for_selected() -> void:
	if _selected_row == null or _selected_row.target == null:
		return
	_popup.set_spawner_options(spawner_options)
	_popup.open_join(_selected_row.target, _last_username)


func _open_edit_for_selected() -> void:
	if _selected_row == null or not _selected_row.target.is_direct():
		return
	_popup.set_templates(backend_templates)
	_popup.open_edit(_selected_row.target)


func _remove_selected() -> void:
	if _selected_row == null or not _selected_row.target.is_direct():
		return
	session.remove_target(_selected_row.target, true)


func _on_target_submitted(target: JoinTarget, persist: bool) -> void:
	if not session.get_direct_targets().has(target):
		session.add_target(target, persist)
	elif persist:
		session.save_server_list()
	_clear_selection()
	session.refresh()


func _on_host_submitted(
	config: ConnectHostConfig, payload: JoinPayload
) -> void:
	_last_username = String(payload.username)
	await session.host(config, payload)


func _on_join_submitted(payload: JoinPayload) -> void:
	_last_username = String(payload.username)
	var target := _selected_row.target if _selected_row else null
	if target == null:
		return
	await session.join(target, payload)


func _on_session_entered() -> void:
	if hide_when_session_active:
		hide()


func _on_session_left() -> void:
	if hide_when_session_active:
		show()


func _on_join_failed(_target: JoinTarget, reason: String) -> void:
	_show_banner(reason)


func _on_provider_unavailable(
	_provider_id: StringName,
	reason: String,
) -> void:
	_show_banner(reason)


func _show_banner(reason: String) -> void:
	if _banner_label == null or _banner == null:
		push_warning("ConnectBrowser: %s" % reason)
		return
	_banner_label.text = reason
	_banner.visible = true


func _status_text(result: ServerInfoResult) -> String:
	if result == null:
		return "..."
	match result.status:
		ServerInfoResult.Status.OK: return "OK"
		ServerInfoResult.Status.BUSY: return "BUSY"
		ServerInfoResult.Status.UNREACHABLE: return "UNREACHABLE"
		ServerInfoResult.Status.TIMEOUT: return "TIMEOUT"
		ServerInfoResult.Status.UNSUPPORTED: return "UNSUPPORTED"
		_: return "ERROR"


func _players_text(result: ServerInfoResult) -> String:
	if result == null or result.info == null:
		return "-"
	return "%d/%d" % [result.info.players, result.info.max_players]
