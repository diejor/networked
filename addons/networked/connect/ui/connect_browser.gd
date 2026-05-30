## A drop-in server browser UI -- server list, Add / Host / Refresh, and a
## join flow, ready to use.
##
## Drop this scene into your lobby, point it at your [MultiplayerTree], and
## players can browse saved servers, watch live status, host a new game, or
## join one with no glue code. Under the hood it drives the tree's canonical
## [ConnectSession] through a [NetwConnect] for you.
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


## The [MultiplayerTree] whose canonical [ConnectSession] this browser
## drives, accessed through a [NetwConnect] facade.
@export var tree: MultiplayerTree:
	set(value):
		if _tree == value:
			return
		_unbind_session_signals()
		_tree = value
		_connect = null
		if is_inside_tree():
			_resolve_connect()
			_bind_session_signals()
			_load_server_list()
			_rebuild_from_session()
			_clear_selection()
			if _connect:
				_connect.refresh()
	get:
		return _tree

## Backends offered by the Add Server and Host popups.
@export var backend_templates: Array[BackendPeer] = []

## Spawner picker choices shown in the Host / Join popup.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:SpawnerComponent",
)
var spawner_options: Array[SceneNodePath] = []

## When [code]true[/code], hides this browser on
## [signal NetwConnect.session_entered] and shows it again on
## [signal NetwConnect.session_left].
@export var hide_when_session_active: bool = true

## Path used to load and persist saved targets shown by this browser.
@export var server_list_path: String = ServerList.DEFAULT_PATH


var _popup: ConnectPopup
var _row_menu: PopupMenu
var _connect: NetwConnect
var _tree: MultiplayerTree
var _rows: Dictionary = {}  # JoinTarget -> ConnectBrowserRow
var _selected_row: ConnectBrowserRow
var _last_username: String = "Player"


@onready var _count_label: Label = %CountLabel
@onready var _refresh_button: Button = %RefreshButton
@onready var _add_button: Button = %AddButton
@onready var _join_direct_button: Button = %JoinDirectButton
@onready var _host_button: Button = %HostButton
@onready var _list_box: VBoxContainer = %ListBox
@onready var _empty_state: VBoxContainer = %EmptyState
@onready var _details_label: Label = %DetailsLabel
@onready var _banner: HBoxContainer = %Banner
@onready var _banner_label: Label = %BannerLabel


func _ready() -> void:
	if _connect == null:
		_resolve_connect()
	_load_server_list()

	_popup = _POPUP_SCENE.instantiate()
	add_child(_popup)
	_popup.set_templates(backend_templates)
	_popup.set_spawner_options(spawner_options)
	_popup.target_submitted.connect(_on_target_submitted)
	_popup.host_submitted.connect(_on_host_submitted)
	_popup.join_submitted.connect(_on_join_submitted)
	_popup.join_direct_submitted.connect(_on_join_direct_submitted)

	_row_menu = PopupMenu.new()
	add_child(_row_menu)
	_row_menu.id_pressed.connect(_on_row_menu_id_pressed)

	_refresh_button.pressed.connect(_on_refresh_pressed)
	_add_button.pressed.connect(_on_add_pressed)
	_join_direct_button.pressed.connect(_on_join_direct_pressed)
	_host_button.pressed.connect(_on_host_pressed)

	_bind_session_signals()

	_rebuild_from_session()
	_clear_selection()
	if _connect:
		_connect.refresh()


func _exit_tree() -> void:
	_unbind_session_signals()


# Resolves the NetwConnect facade from the configured tree.
func _resolve_connect() -> void:
	var origin: Node = _tree if _tree != null else self
	_connect = Netw.ctx(origin).connect


# Loads the browser-owned saved target list into the resolved facade.
func _load_server_list() -> void:
	if _connect != null:
		_connect.load_server_list(server_list_path)


func _bind_session_signals() -> void:
	if _connect == null:
		return
	if not _connect.target_added.is_connected(_on_target_added):
		_connect.target_added.connect(_on_target_added)
	if not _connect.target_removed.is_connected(_on_target_removed):
		_connect.target_removed.connect(_on_target_removed)
	if not _connect.target_updated.is_connected(_on_target_updated):
		_connect.target_updated.connect(_on_target_updated)
	if not _connect.session_entered.is_connected(_on_session_entered):
		_connect.session_entered.connect(_on_session_entered)
	if not _connect.session_left.is_connected(_on_session_left):
		_connect.session_left.connect(_on_session_left)
	if not _connect.host_failed.is_connected(_show_banner):
		_connect.host_failed.connect(_show_banner)
	if not _connect.join_failed.is_connected(_on_join_failed):
		_connect.join_failed.connect(_on_join_failed)
	if not _connect.directory_unavailable.is_connected(
		_on_directory_unavailable
	):
		_connect.directory_unavailable.connect(_on_directory_unavailable)


func _unbind_session_signals() -> void:
	if _connect == null:
		return
	if _connect.target_added.is_connected(_on_target_added):
		_connect.target_added.disconnect(_on_target_added)
	if _connect.target_removed.is_connected(_on_target_removed):
		_connect.target_removed.disconnect(_on_target_removed)
	if _connect.target_updated.is_connected(_on_target_updated):
		_connect.target_updated.disconnect(_on_target_updated)
	if _connect.session_entered.is_connected(_on_session_entered):
		_connect.session_entered.disconnect(_on_session_entered)
	if _connect.session_left.is_connected(_on_session_left):
		_connect.session_left.disconnect(_on_session_left)
	if _connect.host_failed.is_connected(_show_banner):
		_connect.host_failed.disconnect(_show_banner)
	if _connect.join_failed.is_connected(_on_join_failed):
		_connect.join_failed.disconnect(_on_join_failed)
	if _connect.directory_unavailable.is_connected(_on_directory_unavailable):
		_connect.directory_unavailable.disconnect(_on_directory_unavailable)


func _rebuild_from_session() -> void:
	for child in _list_box.get_children():
		child.queue_free()
	_rows.clear()
	if _connect == null:
		_update_counter()
		return
	for target in _connect.get_targets():
		_add_row(target)
	_update_counter()


func _add_row(target: JoinTarget) -> void:
	var row := _ROW_SCENE.instantiate() as ConnectBrowserRow
	_list_box.add_child(row)
	row.bind_target(target)
	var existing := _connect.get_result(target)
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
	
	var is_saved := _connect.get_saved_targets().has(t)
	
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
	lines.append("Saved: %s" % ("Yes" if is_saved else "No - ephemeral"))
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
	
	var is_saved := _connect.get_saved_targets().has(row.target)
	_row_menu.clear()
	_row_menu.add_item("Join", _ROW_MENU_JOIN)
	if is_saved:
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


func _on_join_direct_pressed() -> void:
	_popup.set_templates(backend_templates)
	_popup.open_join_direct(_last_username)


func _on_refresh_pressed() -> void:
	if _connect != null:
		_connect.refresh()


func _on_host_pressed() -> void:
	if _connect == null:
		return
	_popup.set_spawner_options(spawner_options)
	_popup.open_host(backend_templates, _last_username)


func _open_join_for_selected() -> void:
	if _selected_row == null or _selected_row.target == null:
		return
	_popup.set_spawner_options(spawner_options)
	_popup.open_join(_selected_row.target, _last_username)


func _open_edit_for_selected() -> void:
	if _selected_row == null or not _connect.get_saved_targets().has(_selected_row.target):
		return
	_popup.set_templates(backend_templates)
	_popup.open_edit(_selected_row.target)


func _remove_selected() -> void:
	if _selected_row == null or not _connect.get_saved_targets().has(_selected_row.target):
		return
	_connect.remove_target(_selected_row.target, true)


func _on_target_submitted(target: JoinTarget, persist: bool) -> void:
	if not _connect.get_saved_targets().has(target):
		_connect.add_target(target, persist)
	elif persist:
		_connect.save_server_list(server_list_path)
	_clear_selection()
	_connect.refresh()


func _on_host_submitted(
	config: ConnectHostConfig, payload: JoinPayload
) -> void:
	_last_username = String(payload.username)
	await _connect.host(config, payload)


func _on_join_submitted(payload: JoinPayload) -> void:
	_last_username = String(payload.username)
	var target := _selected_row.target if _selected_row else null
	if target == null:
		return
	await _connect.join(target, payload)


func _on_join_direct_submitted(target: JoinTarget, payload: JoinPayload) -> void:
	_last_username = String(payload.username)
	await _connect.join(target, payload)


func _on_session_entered() -> void:
	if hide_when_session_active:
		hide()


func _on_session_left() -> void:
	if hide_when_session_active:
		show()


func _on_join_failed(_target: JoinTarget, reason: String) -> void:
	_show_banner(reason)


func _on_directory_unavailable(
	_directory_id: StringName,
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
