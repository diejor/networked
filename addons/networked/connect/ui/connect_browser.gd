## A drop-in server browser UI -- server list, Add / Host / Refresh, and a
## join flow, ready to use.
##
## Drop this scene into your lobby, point it at your [MultiplayerTree], and
## players can browse saved servers, watch live status, host a new game, or
## join one with no glue code. Under the hood it drives the tree's canonical
## [ConnectSession] through a [NetwConnect] for you.
##
## [br][br]
## The browser finds its session in three steps, first wins: an explicit
## [method bind], then the [member tree] export, then its own ancestry. Drop it
## under the tree for zero config, or hand a parent owned facade through
## [method bind] when it lives elsewhere in the scene. Lobby directories are
## discovered from the tree by [ConnectSession], so there is no per directory
## wiring here.
class_name ConnectBrowser
extends Control

const _ROW_SCENE := preload(
	"res://addons/networked/connect/ui/row.tscn"
)
const _ADD_POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/popups/add_popup.tscn"
)
const _HOST_POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/popups/host_popup.tscn"
)
const _JOIN_POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/popups/join_popup.tscn"
)
const _JOIN_DIRECT_POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/popups/join_direct_popup.tscn"
)
const _CONNECTING_POPUP_SCENE := preload(
	"res://addons/networked/connect/ui/popups/connecting_popup.tscn"
)
const _DETAIL_ITEM_SCENE := preload(
	"res://addons/networked/connect/ui/detail_item.tscn"
)
const _MENU_SCENE := preload(
	"res://addons/networked/connect/ui/popups/menu.tscn"
)

const _ROW_MENU_JOIN := Menu.ID_JOIN
const _ROW_MENU_EDIT := Menu.ID_EDIT
const _ROW_MENU_REMOVE := Menu.ID_REMOVE

## The [MultiplayerTree] whose canonical [ConnectSession] this browser
## drives, accessed through a [NetwConnect] facade.
##
## Resolution order is [method bind] first, then this export, then the
## browser's own ancestry. Leave it unset when the browser is a descendant of
## the tree or when a parent calls [method bind].
@export var tree: MultiplayerTree

## Backends offered by the Add Server and Host popups.
@export var backend_templates: Array[BackendPeer] = []

## Spawner picker choices shown in the Host / Join popup.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:MultiplayerEntity",
)
var spawner_options: Array[SceneNodePath] = []

## When [code]true[/code], hides this browser on
## [signal NetwConnect.session_entered] and shows it again on
## [signal NetwConnect.session_left].
@export var hide_when_session_active: bool = true

## Path used to load and persist saved targets shown by this browser.
@export var server_list_path: String = ServerList.DEFAULT_PATH

var _add_popup: AddPopup
var _host_popup: HostPopup
var _join_popup: JoinPopup
var _join_direct_popup: JoinDirectPopup
var _connecting_popup: ConnectingPopup
var _row_menu: Menu

var _tree: MultiplayerTree
var _rows: Dictionary = { } # JoinTarget -> ConnectBrowserRow
var _selected_row: ConnectBrowserRow
var _last_username: String = "Player"
var _last_join_payload: JoinPayload = null

# Facade supplied by bind(); takes priority over export/ancestry resolution.
var _bound_connect: NetwConnect
# Guards _setup_session against running twice (bind() then the deferred path).
var _session_ready: bool = false

@onready var _refresh_button: Button = %RefreshButton
@onready var _add_button: Button = %AddButton
@onready var _join_direct_button: Button = %JoinDirectButton
@onready var _host_button: Button = %HostButton
@onready var _list_box: VBoxContainer = %ListBox
@onready var _empty_state: VBoxContainer = %EmptyState
@onready var _details_container: HFlowContainer = %DetailsContainer
@onready var _banner: HBoxContainer = %Banner
@onready var _banner_label: Label = %BannerLabel
@onready var _details_header: HBoxContainer = %DetailsHeader
@onready var _details_status_dot: StatusDot = %DetailsStatusDot
@onready var _details_name_label: Label = %DetailsNameLabel
@onready var _details_badge_label: Label = %DetailsBadgeLabel
@onready var _details_footer: HBoxContainer = %DetailsFooter
@onready var _details_edit_button: Button = %DetailsEditButton
@onready var _details_remove_button: Button = %DetailsRemoveButton
@onready var _details_join_button: Button = %DetailsJoinButton

var _connect: NetwConnect


func _ready() -> void:
	var viewport := get_viewport()
	if viewport:
		viewport.gui_embed_subwindows = true

	_add_popup = _ADD_POPUP_SCENE.instantiate()
	add_child(_add_popup)
	_add_popup.submitted.connect(_on_target_submitted)

	_host_popup = _HOST_POPUP_SCENE.instantiate()
	add_child(_host_popup)
	_host_popup.submitted.connect(_on_host_submitted)

	_join_popup = _JOIN_POPUP_SCENE.instantiate()
	add_child(_join_popup)
	_join_popup.submitted.connect(_on_join_submitted)

	_join_direct_popup = _JOIN_DIRECT_POPUP_SCENE.instantiate()
	add_child(_join_direct_popup)
	_join_direct_popup.submitted.connect(_on_join_direct_submitted)

	_connecting_popup = _CONNECTING_POPUP_SCENE.instantiate()
	add_child(_connecting_popup)
	_connecting_popup.cancelled.connect(_on_popup_cancelled)

	_row_menu = _MENU_SCENE.instantiate() as Menu
	add_child(_row_menu)
	_row_menu.id_pressed.connect(_on_row_menu_id_pressed)

	_refresh_button.pressed.connect(_on_refresh_pressed)
	_add_button.pressed.connect(_on_add_pressed)
	_join_direct_button.pressed.connect(_on_join_direct_pressed)
	_host_button.pressed.connect(_on_host_pressed)

	_details_edit_button.pressed.connect(_on_details_edit_pressed)
	_details_remove_button.pressed.connect(_on_details_remove_pressed)
	_details_join_button.pressed.connect(_on_details_join_pressed)

	_clear_selection()

	# Fallback path: if no parent calls bind() this frame, self-resolve once
	# parent _ready() has had a chance to assign the tree export.
	_setup_session.call_deferred()


func _exit_tree() -> void:
	_unbind_session_signals()


## Drives this browser from [param connect], the resolved [NetwConnect] for the
## target tree. Prefer this over the [member tree] export when the browser does
## not sit under the [MultiplayerTree]. A parent typically calls
## [code]browser.bind(Netw.ctx(tree).connect)[/code].
func bind(connect: NetwConnect) -> void:
	_bound_connect = connect
	if is_inside_tree():
		_setup_session()


# Resolves the facade (bind > tree export > ancestry), wires session signals,
# and pulls the first list. Runs at most once.
func _setup_session() -> void:
	if _session_ready:
		return
	if _bound_connect != null and _bound_connect.is_valid():
		_connect = _bound_connect
	else:
		_connect = Netw.ctx(tree if tree != null else self).connect
	_session_ready = true
	_connect.load_server_list(server_list_path)
	_bind_session_signals()
	_rebuild_from_session()
	_connect.refresh()
	# Catch up when the tree entered before this browser bound, e.g. a debug
	# auto-connect: session_entered already fired, so apply its effect now.
	if _connect.is_session_active():
		_on_session_entered()


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
	if not _connect.join_progress.is_connected(_on_join_progress):
		_connect.join_progress.connect(_on_join_progress)
	if not _connect.directory_unavailable.is_connected(
		_on_directory_unavailable,
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
	if _connect.join_progress.is_connected(_on_join_progress):
		_connect.join_progress.disconnect(_on_join_progress)
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
	for child in _details_container.get_children():
		child.queue_free()

	if _selected_row == null or _selected_row.target == null:
		_details_header.visible = false
		_details_footer.visible = false
		_details_status_dot.bind_result(null)
		var empty_lbl := Label.new()
		empty_lbl.text = "Select a server to see details"
		empty_lbl.add_theme_color_override("font_color", Color(0.5, 0.5, 0.5))
		_details_container.add_child(empty_lbl)
		return

	var t := _selected_row.target
	var r := _selected_row.result
	var is_saved := _connect.get_saved_targets().has(t)
	var unavailable := t.backend != null and not t.backend.is_available()

	# Update the Header elements
	_details_header.visible = true
	_details_footer.visible = true
	_details_edit_button.disabled = not is_saved
	_details_remove_button.disabled = not is_saved
	_details_join_button.disabled = unavailable
	_details_name_label.text = _selected_row._display_name()

	var backend_label := "unknown"
	if t.backend != null:
		backend_label = ConnectUiShared.format_backend_label(t.backend)
	_details_badge_label.text = backend_label

	# Update Details Status Dot
	if unavailable:
		_details_status_dot.bind_unavailable()
	else:
		_details_status_dot.bind_result(r)

	# Populate Flow Details
	_details_container.add_child(
		_create_detail_item("Address", ConnectUiShared.format_address(t)),
	)
	_details_container.add_child(
		_create_detail_item(
			"Status",
			"Unavailable" if unavailable else _status_text(r),
		),
	)
	if r == null or r.latency_ms != -1:
		_details_container.add_child(
			_create_detail_item(
				"Latency",
				"%d ms" % r.latency_ms if r and r.is_ok() else "-",
			),
		)
	_details_container.add_child(
		_create_detail_item("Players", _players_text(r)),
	)


func _create_detail_item(
		title: String,
		value: String,
) -> DetailItem:
	var item := _DETAIL_ITEM_SCENE.instantiate() as DetailItem
	item.name = title.to_camel_case() + "Detail"
	item.set_detail(title, value)
	return item


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
	_row_menu.show_for_target(is_saved, screen_position)


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
	_add_popup.set_templates(ConnectSession.available_templates(backend_templates))
	_add_popup.open_add()


func _on_join_direct_pressed() -> void:
	_join_direct_popup.open_join_direct(
		ConnectSession.available_templates(backend_templates),
		spawner_options,
		_last_username,
	)


func _on_refresh_pressed() -> void:
	if _connect != null:
		_connect.refresh()


func _on_host_pressed() -> void:
	if _connect == null:
		return
	_host_popup.open_host(
		ConnectSession.hostable_templates(backend_templates),
		spawner_options,
		_last_username,
	)


func _open_join_for_selected() -> void:
	if _selected_row == null or _selected_row.target == null:
		return
	_join_popup.open_join(spawner_options, _last_username)


func _open_edit_for_selected() -> void:
	if _selected_row == null:
		return
	var is_saved := _connect.get_saved_targets().has(
		_selected_row.target,
	)
	if not is_saved:
		return
	_add_popup.set_templates(ConnectSession.available_templates(backend_templates))
	_add_popup.open_edit(_selected_row.target)


func _remove_selected() -> void:
	if _selected_row == null or not _connect.get_saved_targets().has(_selected_row.target):
		return
	_connect.remove_target(_selected_row.target, true)


func _on_target_submitted(target: JoinTarget) -> void:
	if not _connect.get_saved_targets().has(target):
		_connect.add_target(target, true)
	else:
		_connect.save_server_list(server_list_path)
	_clear_selection()
	_connect.refresh()


func _on_host_submitted(
		config: ConnectHostConfig,
		payload: JoinPayload,
) -> void:
	_hide_banner()
	_last_username = String(payload.username)
	await _connect.host(config, payload)


func _on_join_submitted(payload: JoinPayload) -> void:
	var target: JoinTarget = null
	if _selected_row != null:
		target = _selected_row.target
	if target == null:
		return
	_join_with_preflight(target, payload)


func _on_join_direct_submitted(
		target: JoinTarget,
		payload: JoinPayload,
) -> void:
	_join_with_preflight(target, payload)


func _on_session_entered() -> void:
	_hide_connecting_overlay()
	_hide_banner()
	if hide_when_session_active:
		hide()


func _on_session_left() -> void:
	_hide_connecting_overlay()
	if hide_when_session_active:
		show()


func _on_join_failed(target: JoinTarget, result: ConnectResult) -> void:
	if result != null and result.status == ConnectResult.Status.ABORTED:
		_hide_connecting_overlay()
		return
	var msg := ConnectUiShared.format_connect_error(result)
	_show_banner(msg)
	var detail := ConnectUiShared.format_connect_detail(result)
	_connecting_popup.show_failed(msg, detail)


func _on_join_progress(
		_target: JoinTarget,
		step: StringName,
		message: String,
		ratio: float,
) -> void:
	_connecting_popup.update_progress(step, message, ratio)


func _show_connecting_overlay(target: JoinTarget) -> void:
	_connecting_popup.open_connecting(target)
	$VBox.modulate.a = 0.5


func _hide_connecting_overlay() -> void:
	_connecting_popup.hide()
	$VBox.modulate.a = 1.0


func _on_popup_cancelled() -> void:
	_connect.abort_join()


func _join_with_preflight(
		target: JoinTarget,
		payload: JoinPayload,
) -> void:
	_hide_banner()
	_last_join_payload = payload
	_last_username = String(payload.username)
	if target.backend != null and not target.backend.is_available():
		_show_banner("This transport is not available on this platform.")
		return
	var result := _connect.get_result(target)
	if result != null and result.status == ServerInfoResult.Status.INCOMPATIBLE:
		_show_banner(
			"Incompatible game build; this server runs a different version.",
		)
		return
	_show_connecting_overlay(target)
	var err := await _connect.join(target, payload)
	if err == OK:
		_hide_connecting_overlay()


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


func _hide_banner() -> void:
	if _banner != null:
		_banner.visible = false


func _status_text(result: ServerInfoResult) -> String:
	if result == null:
		return "..."
	match result.status:
		ServerInfoResult.Status.OK:
			return "OK"
		ServerInfoResult.Status.BUSY:
			return "BUSY"
		ServerInfoResult.Status.UNREACHABLE:
			return "UNREACHABLE"
		ServerInfoResult.Status.TIMEOUT:
			return "TIMEOUT"
		ServerInfoResult.Status.UNSUPPORTED:
			return "UNSUPPORTED"
		ServerInfoResult.Status.INCOMPATIBLE:
			return "INCOMPATIBLE"
		_:
			return "ERROR"


func _players_text(result: ServerInfoResult) -> String:
	if result == null or result.info == null:
		return "-"
	return "%d/%d" % [result.info.players, result.info.max_players]


func _on_details_edit_pressed() -> void:
	_open_edit_for_selected()


func _on_details_remove_pressed() -> void:
	_remove_selected()


func _on_details_join_pressed() -> void:
	_open_join_for_selected()
