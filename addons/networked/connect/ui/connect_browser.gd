## A drop-in server browser UI containing a server list, Add / Host / Refresh, and a
## join flow, ready to use.
##
## Drop this scene into your lobby, point it at your [MultiplayerTree], and
## players can browse saved servers, watch live status, host a new game, or
## join one with no glue code.
##
## [br][br]
## It binds exactly two objects, and the split is the whole design: a
## [NetwServerBrowser] answers what is out there, and a [NetwMultiplayer]
## answers what happens when a row is picked. Neither can do the other's job.
## [codeblock]
## browser.bind(Netw.of(tree), NetwServerBrowser.new(Netw.of(tree)))
## [/codeblock]
## The browser finds its session in three steps, first wins: an explicit
## [method bind], then the [member tree] export, then its own ancestry. Drop it
## under the tree for zero config. Transports come from
## [method NetwTransport.registered] and lobby directories from the session's
## service registry, so there is no per browser wiring here.
class_name ConnectBrowser
extends Control

var _ROW_SCENE := load(
	"res://addons/networked/connect/ui/row.tscn",
)
var _ADD_POPUP_SCENE := load(
	"res://addons/networked/connect/ui/popups/add_popup.tscn",
)
var _HOST_POPUP_SCENE := load(
	"res://addons/networked/connect/ui/popups/host_popup.tscn",
)
var _JOIN_POPUP_SCENE := load(
	"res://addons/networked/connect/ui/popups/join_popup.tscn",
)
var _JOIN_DIRECT_POPUP_SCENE := load(
	"res://addons/networked/connect/ui/popups/join_direct_popup.tscn",
)
var _CONNECTING_POPUP_SCENE := load(
	"res://addons/networked/connect/ui/popups/connecting_popup.tscn",
)
var _DETAIL_ITEM_SCENE := load(
	"res://addons/networked/connect/ui/detail_item.tscn",
)
var _MENU_SCENE := load(
	"res://addons/networked/connect/ui/popups/menu.tscn",
)

const _ROW_MENU_JOIN := Menu.ID_JOIN
const _ROW_MENU_EDIT := Menu.ID_EDIT
const _ROW_MENU_REMOVE := Menu.ID_REMOVE

## Default server name used when none is provided.
const PLACEHOLDER_SERVER_NAME := "My Server"

## The [MultiplayerTree] whose session this browser drives.
##
## Resolution order is [method bind] first, then this export, then the
## browser's own ancestry. Leave it unset when the browser is a descendant of
## the tree or when a parent calls [method bind].
@export var tree: MultiplayerTree

## Spawner picker choices shown in the Host / Join popup.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:Node",
)
var spawner_options: Array[SceneNodePath] = []

## When [code]true[/code], hides this browser on
## [signal NetwMultiplayer.session_entered] and shows it again on
## [signal NetwMultiplayer.session_ended].
@export var hide_when_session_active: bool = true

## Path used to load and persist saved targets shown by this browser.
@export var server_list_path: String = NetwServerList.DEFAULT_PATH

var _add_popup: AddPopup
var _host_popup: HostPopup
var _join_popup: JoinPopup
var _join_direct_popup: JoinDirectPopup
var _connecting_popup: ConnectingPopup
var _row_menu: Menu

var _rows: Dictionary = { } # NetwConnectTarget -> ConnectBrowserRow
var _selected_row: ConnectBrowserRow
var _last_username: String = "Player"
var _last_join_payload: JoinPayload = null
# The target of the bring-up in flight, null while hosting. A failure is
# reported against what the player picked, not against whichever attempt lost.
var _last_connect_target: NetwConnectTarget = null

# Objects supplied by bind(); take priority over export/ancestry resolution.
var _bound_api: NetwMultiplayer
var _bound_model: NetwServerBrowser
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

var _api: NetwMultiplayer
var _model: NetwServerBrowser


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


## Drives this browser from [param api] and [param model].
##
## Prefer this over the [member tree] export when the browser does not sit under
## the [MultiplayerTree]. Passing a [code]null[/code] [param model] builds one
## over [param api], which is what a caller that only wants the default browse
## behavior does.
func bind(api: NetwMultiplayer, model: NetwServerBrowser = null) -> void:
	_bound_api = api
	_bound_model = model if model else (NetwServerBrowser.new(api) if api else null)
	if is_inside_tree():
		_setup_session()


# Resolves the session (bind > tree export > ancestry), wires its signals, and
# pulls the first list. Runs at most once.
func _setup_session() -> void:
	if _session_ready:
		return
	if _bound_api != null:
		_api = _bound_api
		_model = _bound_model
	else:
		_api = Netw.of(tree if tree != null else self)
		_model = NetwServerBrowser.new(_api) if _api else null
	if _api == null or _model == null:
		return
	_session_ready = true
	_model.load_server_list(server_list_path)
	_bind_session_signals()
	_rebuild_from_session()
	_model.refresh()
	# Catch up when the tree entered before this browser bound, e.g. a debug
	# auto-connect: the session is already online, so apply its effect now.
	if _api.is_online:
		_on_session_entered()


func _bind_session_signals() -> void:
	if _model == null or _api == null:
		return
	if not _model.target_added.is_connected(_on_target_added):
		_model.target_added.connect(_on_target_added)
	if not _model.target_removed.is_connected(_on_target_removed):
		_model.target_removed.connect(_on_target_removed)
	if not _model.target_updated.is_connected(_on_target_updated):
		_model.target_updated.connect(_on_target_updated)
	if not _model.directory_unavailable.is_connected(_on_directory_unavailable):
		_model.directory_unavailable.connect(_on_directory_unavailable)
	if not _api.session_entered.is_connected(_on_session_entered):
		_api.session_entered.connect(_on_session_entered)
	if not _api.session_ended.is_connected(_on_session_left):
		_api.session_ended.connect(_on_session_left)
	# Progress is per attempt and an outcome is per verb call, so the two are
	# bound separately: one bring-up can raise more than one attempt.
	var connector := NetwConnector.of(_api)
	if not connector.attempt_started.is_connected(_on_attempt_started):
		connector.attempt_started.connect(_on_attempt_started)
	if not connector.finished.is_connected(_on_connect_finished):
		connector.finished.connect(_on_connect_finished)


func _unbind_session_signals() -> void:
	if _model != null:
		if _model.target_added.is_connected(_on_target_added):
			_model.target_added.disconnect(_on_target_added)
		if _model.target_removed.is_connected(_on_target_removed):
			_model.target_removed.disconnect(_on_target_removed)
		if _model.target_updated.is_connected(_on_target_updated):
			_model.target_updated.disconnect(_on_target_updated)
		if _model.directory_unavailable.is_connected(_on_directory_unavailable):
			_model.directory_unavailable.disconnect(_on_directory_unavailable)
	if _api == null:
		return
	if _api.session_entered.is_connected(_on_session_entered):
		_api.session_entered.disconnect(_on_session_entered)
	if _api.session_ended.is_connected(_on_session_left):
		_api.session_ended.disconnect(_on_session_left)
	var connector := NetwConnector.of(_api)
	if connector.attempt_started.is_connected(_on_attempt_started):
		connector.attempt_started.disconnect(_on_attempt_started)
	if connector.finished.is_connected(_on_connect_finished):
		connector.finished.disconnect(_on_connect_finished)


# Feeds the connecting overlay from one attempt's progress. Nothing terminal is
# read here: a host that finds the port taken raises a second attempt and lands
# in the session, and the failure of the first is not something a player did.
func _on_attempt_started(attempt: NetwConnectAttempt) -> void:
	var target := attempt.target
	var progress_cb := func(step: StringName, message: String, ratio: float) -> void:
		_on_join_progress(target, step, message, ratio)
	attempt.progress.connect(progress_cb)
	attempt.finished.connect(
		func(_result: NetwConnectResult) -> void:
			if attempt.progress.is_connected(progress_cb):
				attempt.progress.disconnect(progress_cb),
		CONNECT_ONE_SHOT,
	)


# Reports the outcome of a whole bring-up. This is the only place a banner is
# raised, so what the player sees is what the verb they pressed did.
func _on_connect_finished(result: NetwConnectResult) -> void:
	if result == null or result.is_ok():
		return
	if result.status == NetwConnectResult.Status.ABORTED:
		_hide_connecting_overlay()
		return
	var target := _last_connect_target
	if target == null:
		_show_banner(_reason(result))
		_connecting_popup.show_failed(_reason(result), "")
	else:
		_on_join_failed(target, result)


# The message of a failed result, or a generic fallback.
func _reason(result: NetwConnectResult) -> String:
	if result == null or result.message.is_empty():
		return "Connection failed."
	return result.message


func _rebuild_from_session() -> void:
	for child in _list_box.get_children():
		child.queue_free()
	_rows.clear()
	if _model == null:
		_update_counter()
		return
	for target in _model.targets:
		_add_row(target)
	_update_counter()


func _add_row(target: NetwConnectTarget) -> void:
	var row := _ROW_SCENE.instantiate() as ConnectBrowserRow
	_list_box.add_child(row)
	row.bind_target(target)
	var existing := _model.get_result(target)
	if existing != null:
		row.set_result(existing)
	row.selected.connect(_on_row_selected.bind(row))
	row.context_requested.connect(_on_row_context_requested)
	row.activated.connect(_on_row_activated)
	_rows[target] = row


func _on_target_added(target: NetwConnectTarget) -> void:
	if _rows.has(target):
		return
	_add_row(target)
	_update_counter()


func _on_target_removed(target: NetwConnectTarget) -> void:
	var row: ConnectBrowserRow = _rows.get(target)
	if row != null:
		row.queue_free()
		_rows.erase(target)
	if _selected_row == row:
		_clear_selection()
	_update_counter()


func _on_target_updated(target: NetwConnectTarget, result: NetwProbeResult) -> void:
	var row: ConnectBrowserRow = _rows.get(target)
	if row != null:
		row.set_result(result)
	if _selected_row != null and _selected_row.target == target:
		_update_details()


func _update_counter() -> void:
	var total := _rows.size()
	_empty_state.visible = total == 0


func _on_row_selected(_target: NetwConnectTarget, row: ConnectBrowserRow) -> void:
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
	var is_saved := _model.saved_targets.has(t)
	var unavailable := not _model.is_target_available(t)

	# Update the Header elements
	_details_header.visible = true
	_details_footer.visible = true
	_details_edit_button.disabled = not is_saved
	_details_remove_button.disabled = not is_saved
	_details_join_button.disabled = unavailable
	_details_name_label.text = _selected_row._display_name()
	_details_badge_label.text = ConnectBrowser.format_scheme_label(t.scheme)

	# Update Details Status Dot
	if unavailable:
		_details_status_dot.bind_unavailable()
	else:
		_details_status_dot.bind_result(r)

	# Populate Flow Details
	_details_container.add_child(
		_create_detail_item("Address", ConnectBrowser.format_address(t)),
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
		_target: NetwConnectTarget,
		row: ConnectBrowserRow,
		screen_position: Vector2,
) -> void:
	if row == null or row.target == null:
		return
	_on_row_selected(row.target, row)
	row.button_pressed = true

	var is_saved := _model.saved_targets.has(row.target)
	_row_menu.show_for_target(is_saved, screen_position)


func _on_row_menu_id_pressed(id: int) -> void:
	match id:
		_ROW_MENU_JOIN:
			_open_join_for_selected()
		_ROW_MENU_EDIT:
			_open_edit_for_selected()
		_ROW_MENU_REMOVE:
			_remove_selected()


func _on_row_activated(_target: NetwConnectTarget, row: ConnectBrowserRow) -> void:
	if row == null or row.target == null:
		return
	_on_row_selected(row.target, row)
	row.button_pressed = true
	_open_join_for_selected()


func _on_add_pressed() -> void:
	_add_popup.set_transports(_model.available_transports())
	_add_popup.open_add()


func _on_join_direct_pressed() -> void:
	_join_direct_popup.open_join_direct(
		_model.available_transports(),
		spawner_options,
		_last_username,
	)


func _on_refresh_pressed() -> void:
	if _model != null:
		_model.refresh()


func _on_host_pressed() -> void:
	if _model == null or _api == null:
		return
	_host_popup.open_host(
		_model.hostable_transports(),
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
	var is_saved := _model.saved_targets.has(
		_selected_row.target,
	)
	if not is_saved:
		return
	_add_popup.set_transports(_model.available_transports())
	_add_popup.open_edit(_selected_row.target)


func _remove_selected() -> void:
	if _selected_row == null or not _model.saved_targets.has(_selected_row.target):
		return
	_model.remove_target(_selected_row.target, true)


func _on_target_submitted(target: NetwConnectTarget) -> void:
	if not _model.saved_targets.has(target):
		_model.add_target(target, true)
	else:
		_model.save_server_list(server_list_path)
	_clear_selection()
	_model.refresh()


func _on_host_submitted(
		config: NetwHostConfig,
		payload: JoinPayload,
) -> void:
	_hide_banner()
	_last_username = String(payload.username)
	_last_connect_target = null
	_show_connecting_overlay(null)
	await NetwConnector.of(_api).host(payload, config)


func _on_join_submitted(payload: JoinPayload) -> void:
	var target: NetwConnectTarget = null
	if _selected_row != null:
		target = _selected_row.target
	if target == null:
		return
	_join_with_preflight(target, payload)


func _on_join_direct_submitted(
		target: NetwConnectTarget,
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


func _on_join_failed(target: NetwConnectTarget, result: NetwConnectResult) -> void:
	if result != null and result.status == NetwConnectResult.Status.ABORTED:
		_hide_connecting_overlay()
		return
	var msg := ConnectBrowser.format_connect_error(result)
	_show_banner(msg)
	var detail := ConnectBrowser.format_connect_detail(result)
	_connecting_popup.show_failed(msg, detail)


func _on_join_progress(
		_target: NetwConnectTarget,
		step: StringName,
		message: String,
		ratio: float,
) -> void:
	_connecting_popup.update_progress(step, message, ratio)


func _show_connecting_overlay(target: NetwConnectTarget) -> void:
	_connecting_popup.open_connecting(target)
	$VBox.modulate.a = 0.5


func _hide_connecting_overlay() -> void:
	_connecting_popup.hide()
	$VBox.modulate.a = 1.0


func _on_popup_cancelled() -> void:
	NetwConnector.of(_api).abort()


func _join_with_preflight(
		target: NetwConnectTarget,
		payload: JoinPayload,
) -> void:
	_hide_banner()
	_last_join_payload = payload
	_last_username = String(payload.username)
	if not _model.is_target_available(target):
		_show_banner("This transport is not available on this platform.")
		return
	var result := _model.get_result(target)
	if result != null and result.status == NetwProbeResult.Status.INCOMPATIBLE:
		_show_banner(
			"Incompatible game build; this server runs a different version.",
		)
		return
	_last_connect_target = target
	_show_connecting_overlay(target)
	var joined := await NetwConnector.of(_api).join(target, payload, true)
	if joined.is_ok():
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


func _status_text(result: NetwProbeResult) -> String:
	if result == null:
		return "..."
	match result.status:
		NetwProbeResult.Status.OK:
			return "OK"
		NetwProbeResult.Status.BUSY:
			return "BUSY"
		NetwProbeResult.Status.UNREACHABLE:
			return "UNREACHABLE"
		NetwProbeResult.Status.TIMEOUT:
			return "TIMEOUT"
		NetwProbeResult.Status.UNSUPPORTED:
			return "UNSUPPORTED"
		NetwProbeResult.Status.INCOMPATIBLE:
			return "INCOMPATIBLE"
		_:
			return "ERROR"


func _players_text(result: NetwProbeResult) -> String:
	if result == null or result.info == null:
		return "-"
	return "%d/%d" % [result.info.players, result.info.max_players]


func _on_details_edit_pressed() -> void:
	_open_edit_for_selected()


func _on_details_remove_pressed() -> void:
	_remove_selected()


func _on_details_join_pressed() -> void:
	_open_join_for_selected()


## Human-readable label for a transport [param scheme], such as
## [code]&"enet"[/code] -> "Enet". "-" when empty.
static func format_scheme_label(scheme: StringName) -> String:
	var text := String(scheme)
	if text.is_empty():
		return "-"
	return text.capitalize()


## Displayable address for [param target]. Either its explicit address, the
## owning transport's placeholder when an empty address means a local default,
## or "-".
static func format_address(target: NetwConnectTarget) -> String:
	if target == null:
		return "-"
	var address := target.address.strip_edges()
	if not address.is_empty():
		return address
	for transport in NetwTransport.registered():
		if transport._scheme() != target.scheme:
			continue
		var hint := transport._address_hint()
		if hint and hint.accepts_empty and not hint.placeholder.is_empty():
			return hint.placeholder
		break
	return "-"


## Label for a [SceneNodePath] spawner option in the picker.
static func format_spawner_label(path: SceneNodePath) -> String:
	if path == null:
		return "(none)"
	if path.node_path.is_empty():
		return path.scene_path
	return path.node_path


## Returns a user-friendly error string for [param result].
static func format_connect_error(result: NetwConnectResult) -> String:
	if result == null:
		return "Unknown error."
	match result.status:
		NetwConnectResult.Status.OK:
			return "Success."
		NetwConnectResult.Status.TIMED_OUT:
			return "Connection timed out."
		NetwConnectResult.Status.REFUSED:
			return "Connection refused."
		NetwConnectResult.Status.ABORTED:
			return "Connection aborted by user."
		NetwConnectResult.Status.UNREACHABLE:
			match result.detail:
				&"TURN_UNREACHABLE":
					return "Relay server unreachable."
				&"HOST_UNRESPONSIVE":
					return "Host did not respond."
				&"SIGNALING_UNAVAILABLE":
					return "Could not reach signaling."
				&"SIGNALING_UNREACHABLE":
					return "No signaling server reachable."
				&"NAT_TRAVERSAL_FAILED":
					return "Could not establish a direct connection."
				&"STEAM_P2P_FAILED":
					return "Steam peer connection failed."
				&"PEER_CONNECT_FAILED":
					return "Could not reach the server."
				_:
					return "Server unreachable."
		_:
			if not result.message.is_empty():
				return result.message
			return "Connection failed."


## Returns an optional second line for useful [NetwConnectResult] diagnostics.
static func format_connect_detail(result: NetwConnectResult) -> String:
	if result == null:
		return ""
	var stats: Dictionary = result.diagnostics.get("candidates", { })
	if stats.is_empty():
		return ""
	var host_count := int(stats.get("host", 0))
	var srflx_count := int(stats.get("srflx", 0))
	var relay_count := int(stats.get("relay", 0))
	if host_count == 0 and srflx_count == 0 and relay_count == 0:
		return "No connection candidates were gathered."
	if bool(result.diagnostics.get("relay_used", false)):
		return "Only relay candidates were gathered."
	return ""
