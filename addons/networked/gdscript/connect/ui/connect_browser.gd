## A drop-in server browser UI containing a server list, Add / Host /
## Refresh, and a join flow, ready to use.
##
## Drop this scene anywhere under a session and players can browse saved
## servers, watch live status, host a new game, or join one with no glue
## code. It is a client of one object, [NetwConnectHandle], reached as
## [method Netw.connection]. Everything this browser draws (the list, the
## Host and Join forms, progress and outcomes) comes from that handle.
## [codeblock]
## browser.bind(Netw.connection(other_node))
## [/codeblock]
## The browser finds its handle in two steps, first wins: an explicit
## [method bind], then [method Netw.connection] over its own ancestry. Drop
## it under a session for zero config.
## [br][br]
## Pressing Host or Join runs one setup the browser composes itself, out of
## three ordinary steps: [method NetwConnectHandle.create_peer] asks the
## provider for a peer, [method Netw.join] arranges the player the form
## collected, and the peer is assigned. The assignment
## happens inside the creation callback, which is the only window the seam
## offers, and progress comes from that one operation rather than from
## anything session-wide. Success is [signal NetwMultiplayer.session_entered],
## which is the event the player was actually waiting for.
## [codeblock]
## create_peer ──> completed(peer, error, detail)
##                     ┠╴ error  ──> banner, and the offer is declined
##                     ┖╴ ok     ──> prepare_join, then assign
##                                       ──> session_entered
## [/codeblock]
## Cancelling withdraws only what this browser owns: the ticket it minted, and
## the peer it assigned while that peer is still the installed one. Once the
## session is entered the setup is over, so closing or freeing the browser
## cannot end a match, and neither can a peer the game assigned meanwhile be
## cleared by a browser the player merely dismissed.
## [br][br]
## Bookmarks are the browser's own, not the session's. What it saved to
## [member server_list_path] it offers back to the session on ready, and only
## those rows can be edited or removed here. Every other row the session
## holds is drawn live and left alone, so two browsers over one session keep
## two separate files.
class_name ConnectBrowser
extends Control

var _ROW_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/row.tscn",
)
var _ADD_POPUP_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/add_popup.tscn",
)
var _HOST_POPUP_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/host_popup.tscn",
)
var _JOIN_POPUP_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/join_popup.tscn",
)
var _JOIN_DIRECT_POPUP_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/join_direct_popup.tscn",
)
var _CONNECTING_POPUP_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/connecting_popup.tscn",
)
var _DETAIL_ITEM_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/detail_item.tscn",
)
const _ICE_SERVERS_EDIT := preload(
	"res://addons/networked/gdscript/connect/ui/ice_servers_edit.tscn"
)
const _STRING_LIST_EDIT := preload(
	"res://addons/networked/gdscript/connect/ui/string_list_edit.tscn"
)
var _MENU_SCENE := load(
	"res://addons/networked/gdscript/connect/ui/popups/menu.tscn",
)

const _ROW_MENU_JOIN := Menu.ID_JOIN
const _ROW_MENU_EDIT := Menu.ID_EDIT
const _ROW_MENU_REMOVE := Menu.ID_REMOVE

## Default server name used when none is provided.
const PLACEHOLDER_SERVER_NAME := "My Server"

## Bookmark file used when [member server_list_path] is empty.
const DEFAULT_SERVER_LIST_PATH := "user://netw_servers.cfg"

## When [code]true[/code], hides this browser once the session comes
## online and shows it again once the session ends.
@export var hide_when_session_active: bool = true

## [ConfigFile] path this browser reads its own bookmarks from and writes
## them back to. Empty picks [constant DEFAULT_SERVER_LIST_PATH], which is
## one file every browser in the project shares, so a scene that wants its
## own bookmarks names its own path here.
##
## The file belongs to this browser and holds only the rows this browser
## authored. A row a directory published, or one another browser added to
## the same session, is drawn from live evidence and never written here.
@export var server_list_path: String = ""

## When [code]true[/code] on a web export, mirrors the hosted room code into
## the page URL's fragment and joins the room a fragment already names.
##
## Hosting rewrites the address bar, so a host shares a link rather than
## reading a code aloud, and opening that link fills the join form. The
## fragment is the only part of a URL a page may rewrite without reloading.
@export var use_url_fragment: bool = true

## Latency and loss to impair every connection this browser starts with, for
## testing a build against a link the developer's own machine does not have.
##
## It reaches the wire through [method NetwLinkConditions.wrap_peer], which
## this browser applies to the peer IT built before assigning it, so the
## impairment lives exactly as long as that peer and a session the game brought
## up itself is never impaired by a browser the player merely opened. An
## impairment the session already consumed through
## [member NetwSessionConfig.link_conditions] wins and this field is ignored,
## because two authored impairments are a mistake rather than a sum.
## [br][br]
## Nothing here reaches a shipped build.
## [method NetwLinkConditions.wrap_peer] gates the whole path and declines in
## a release export, so a scene saved with this authored is inert rather than
## slow.
@export var debug_link: NetwLinkConditions

var _add_popup: AddPopup
var _host_popup: HostPopup
var _join_popup: JoinPopup
var _join_direct_popup: JoinDirectPopup
var _connecting_popup: ConnectingPopup
var _row_menu: Menu

var _rows: Dictionary = { } # endpoint key -> ConnectBrowserRow
# The bookmarks this browser owns, in file order. Each entry is
# { peer_class: StringName, address: String, display_name: String }.
var _records: Array[Dictionary] = []
# endpoint key -> the entry of _records it was minted from. An endpoint
# absent here belongs to a directory or to another browser on the same
# session.
var _mine: Dictionary = { }
var _selected_row: ConnectBrowserRow
var _selected_peer_class: StringName = &""
var _selected_address: String = ""
var _last_username: String = "Player"

# The creation ticket of the setup this browser is running, invalid when none
# is. Cancellation consumes it and the completion callback retires it.
var _ticket: RID
# Counts the setups this browser has started. A completion carrying a stale
# count belongs to a setup that was cancelled or superseded, and declines the
# offer by returning without assigning.
var _setup: int = 0
# The exact peer this browser last assigned. Cancellation clears the session's
# peer only while it is still this reference, so a peer the game assigned in
# the meantime is never cleared by a browser the player merely closed.
var _assigned: MultiplayerPeer
# The transport the running setup asked for, which is what names the address
# the room bar shows: an ENet host is looking at an IP and a signalled one at
# a room code, and only the transport knows which.
var _active_peer_class: StringName = &""

# The handle passed to bind(), taking priority over ancestry resolution.
var _bound_connection: NetwConnectHandle
# Guards _setup_session against running twice: bind() then the deferred path.
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
@onready var _room_bar: PanelContainer = %RoomBar
@onready var _room_label: Label = %RoomLabel
@onready var _room_code_edit: LineEdit = %RoomCodeEdit
@onready var _room_copy_button: Button = %RoomCopyButton

var _connection: NetwConnectHandle
var _api: NetwMultiplayer
var _session: NetwSessionHandle


func _ready() -> void:
	var viewport := get_viewport()
	if viewport:
		viewport.gui_embed_subwindows = true

	_add_popup = _ADD_POPUP_SCENE.instantiate()
	add_child(_add_popup)
	_add_popup.submitted.connect(_on_endpoint_submitted)

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
	_room_copy_button.pressed.connect(_on_room_copy_pressed)

	_clear_selection()

	# Fallback path: if no parent calls bind() this frame, self-resolve once
	# parent _ready() has had a chance to attach the session ancestry.
	_setup_session.call_deferred()


# A browser being freed withdraws the creation it started and nothing else.
# It never touches an assigned peer, because a game that is already in a match
# does not lose it by closing the window it found the match in.
func _exit_tree() -> void:
	if _ticket.is_valid():
		if _connection != null:
			_connection.cancel_peer_creation(_ticket)
		_ticket = RID()
	_setup += 1
	_assigned = null
	_unbind_session_signals()


## Drives this browser from [param handle] instead of resolving one from
## ancestry. Prefer this when the browser does not sit under the session.
func bind(handle: NetwConnectHandle) -> void:
	_bound_connection = handle
	if is_inside_tree():
		_setup_session()


# Resolves the handle (bind() then ancestry), wires its signals, and pulls
# the first list. Runs at most once.
func _setup_session() -> void:
	if _session_ready:
		return
	if _bound_connection != null:
		_connection = _bound_connection
	else:
		_connection = Netw.connection(self)
	_api = Netw.of(self)
	_session = Netw.session(self)
	if _connection == null:
		return
	_session_ready = true
	_bind_session_signals()
	_rebuild_from_session()
	_load_records()
	_publish_records()
	_connection.endpoint_refresh()
	# Catch up when the session entered before this browser bound, e.g. a
	# debug auto-connect: it is already online, so apply its effect now.
	if _session != null and _session.is_online:
		_on_session_entered()
	else:
		_offer_url_room()


func _bind_session_signals() -> void:
	if _connection == null:
		return
	if not _connection.endpoint_added.is_connected(_on_endpoint_added):
		_connection.endpoint_added.connect(_on_endpoint_added)
	if not _connection.endpoint_removed.is_connected(_on_endpoint_removed):
		_connection.endpoint_removed.connect(_on_endpoint_removed)
	if not _connection.endpoint_updated.is_connected(_on_endpoint_updated):
		_connection.endpoint_updated.connect(_on_endpoint_updated)
	if _session == null:
		return
	if not _session.entered.is_connected(_on_session_entered):
		_session.entered.connect(_on_session_entered)
	if not _session.ended.is_connected(_on_session_left):
		_session.ended.connect(_on_session_left)
	if not _session.disconnected.is_connected(_on_session_left):
		_session.disconnected.connect(_on_session_left)


func _unbind_session_signals() -> void:
	if _connection != null:
		if _connection.endpoint_added.is_connected(_on_endpoint_added):
			_connection.endpoint_added.disconnect(_on_endpoint_added)
		if _connection.endpoint_removed.is_connected(_on_endpoint_removed):
			_connection.endpoint_removed.disconnect(_on_endpoint_removed)
		if _connection.endpoint_updated.is_connected(_on_endpoint_updated):
			_connection.endpoint_updated.disconnect(_on_endpoint_updated)
	if _session == null:
		return
	if _session.entered.is_connected(_on_session_entered):
		_session.entered.disconnect(_on_session_entered)
	if _session.ended.is_connected(_on_session_left):
		_session.ended.disconnect(_on_session_left)
	if _session.disconnected.is_connected(_on_session_left):
		_session.disconnected.disconnect(_on_session_left)


func _list_path() -> String:
	if not server_list_path.is_empty():
		return server_list_path
	return DEFAULT_SERVER_LIST_PATH


# A local Dictionary key for one endpoint, never spelled onto the session or
# any published surface. It exists only so this browser can index its own
# rows and bookmarks by the pair an endpoint actually is.
func _key(peer_class: StringName, address: String) -> String:
	return "%s|%s" % [String(peer_class), address]


func _has_selection() -> bool:
	return not _selected_peer_class.is_empty()


func _load_records() -> void:
	_records.clear()
	var config := ConfigFile.new()
	if config.load(_list_path()) != OK:
		return
	for section in config.get_sections():
		var address := str(config.get_value(section, "address", ""))
		var peer_class := StringName(
			config.get_value(section, "peer_class", ""),
		)
		if peer_class.is_empty():
			continue
		var settings: Variant = config.get_value(section, "settings", { })
		_records.append(
			{
				peer_class = peer_class,
				address = address,
				display_name = str(config.get_value(section, "display_name", "")),
				settings = settings if settings is Dictionary else { },
			},
		)


func _save_records() -> void:
	var config := ConfigFile.new()
	for i in _records.size():
		var record: Dictionary = _records[i]
		var section := "server_%d" % i
		config.set_value(section, "peer_class", String(record.peer_class))
		config.set_value(section, "address", record.address)
		config.set_value(section, "display_name", record.display_name)
		var settings: Dictionary = record.get("settings", { })
		if not settings.is_empty():
			config.set_value(section, "settings", settings)
	var err := config.save(_list_path())
	if err != OK:
		push_warning(
			"ConnectBrowser: could not write %s (%s)"
			% [_list_path(), error_string(err)],
		)


# A record naming a transport this build does not carry stays in _records, so
# it survives the next save, and contributes no row.
func _publish_records() -> void:
	for record: Dictionary in _records:
		_publish_record(record)


func _publish_record(record: Dictionary) -> void:
	var added := _connection.endpoint_add(
		record.peer_class,
		record.address,
		record.display_name,
	)
	if added:
		_mine[_key(record.peer_class, record.address)] = record


func _forget_record(peer_class: StringName, address: String) -> void:
	var key := _key(peer_class, address)
	var record: Dictionary = _mine.get(key, { })
	_mine.erase(key)
	if not record.is_empty():
		_records.erase(record)
	_connection.endpoint_remove(peer_class, address)
	_save_records()


# Starts one setup, the provider builds a peer, this browser prepares the
# player it collected, and it assigns the peer. Every step belongs to this
# browser, so nothing else has to be asked what the player pressed.
func _begin_setup(
		peer_class: StringName,
		mode: NetwMultiplayer.TransportMode,
		address: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array,
) -> void:
	_cancel_setup()
	_setup += 1
	_active_peer_class = peer_class
	var mine := _setup
	_show_connecting_overlay(
		peer_class if mode ==
		NetwMultiplayer.TRANSPORT_MODE_CLIENT else &"",
		address,
	)
	_ticket = _connection.create_peer(
		peer_class,
		mode,
		address,
		settings,
		_on_peer_created.bind(mine, username, join_args),
		_on_creation_progress.bind(mine),
	)
	if not _ticket.is_valid():
		_fail_setup(ERR_UNCONFIGURED, "")


func _on_peer_created(
		peer: MultiplayerPeer,
		error: Error,
		detail: String,
		mine: int,
		username: StringName,
		join_args: Array,
) -> void:
	if mine != _setup:
		return
	_ticket = RID()
	if error != OK or peer == null:
		_fail_setup(error if error != OK else ERR_CANT_CREATE, detail)
		return
	if not String(username).is_empty():
		Netw.join(self, username, join_args)
	var shaped := _shaped(peer)
	_api.multiplayer_peer = shaped
	if _api.multiplayer_peer != shaped:
		_fail_setup(ERR_CANT_CONNECT, "the session refused the peer.")
		return
	_assigned = shaped


func _on_creation_progress(
		_step: StringName,
		message: String,
		ratio: float,
		mine: int,
) -> void:
	if mine != _setup:
		return
	_connecting_popup.update_progress(message, ratio)


# The only place a banner is raised, so what the player sees is what the
# thing they pressed did.
func _fail_setup(error: Error, detail: String) -> void:
	var reason := error_string(error)
	_show_banner(reason)
	_connecting_popup.show_failed(reason, detail)


# Wraps the peer this browser had built, so the impairment lives exactly as
# long as the peer the setup assigned and needs nothing taken back afterwards.
func _shaped(peer: MultiplayerPeer) -> MultiplayerPeer:
	if debug_link == null or peer == null or _session == null:
		return peer
	var wrapped := debug_link.wrap_peer(peer)
	if wrapped == null or wrapped == peer:
		return peer
	var config: NetwSessionConfig = _session.config
	if config != null and config.link_conditions != null:
		push_warning(
			"ConnectBrowser: debug_link is ignored because the session "
			+ "already consumed an authored impairment.",
		)
		return peer
	return wrapped


# Abandons the setup in flight, if any. It cancels only work this browser
# owns: the ticket it minted, and the peer it assigned while that peer is
# still the one installed.
func _cancel_setup() -> void:
	_setup += 1
	if _ticket.is_valid():
		if _connection != null:
			_connection.cancel_peer_creation(_ticket)
		_ticket = RID()
	if _assigned != null:
		if _api != null and _api.multiplayer_peer == _assigned:
			_api.multiplayer_peer = null
		_assigned = null


func _rebuild_from_session() -> void:
	for child in _list_box.get_children():
		child.queue_free()
	_rows.clear()
	if _connection == null:
		_update_counter()
		return
	for endpoint: Dictionary in _connection.endpoints():
		_add_row(endpoint)
	_update_counter()


func _add_row(endpoint: Dictionary) -> void:
	var peer_class: StringName = endpoint.get("peer_class")
	var address: String = endpoint.get("address", "")
	var row := _ROW_SCENE.instantiate() as ConnectBrowserRow
	_list_box.add_child(row)
	row.bind_endpoint(_connection, peer_class, address)
	row.set_endpoint(endpoint)
	row.selected.connect(_on_row_selected.bind(row))
	row.context_requested.connect(_on_row_context_requested)
	row.activated.connect(_on_row_activated)
	_rows[_key(peer_class, address)] = row


func _is_available(endpoint: Dictionary) -> bool:
	return bool(endpoint.get("is_available", false))


# A direct address has no endpoint record, so platform support is asked of
# the transport rather than of a row that will never exist.
func _transport_is_available(peer_class: StringName) -> bool:
	var transport := _connection.transport(peer_class)
	if transport.is_empty():
		return false
	var caps := int(transport.get("capabilities", 0))
	return (caps & NetwMultiplayer.TRANSPORT_AVAILABLE) != 0


func _on_endpoint_added(peer_class: StringName, address: String) -> void:
	var key := _key(peer_class, address)
	if _rows.has(key):
		return
	var endpoint := _connection.endpoint(peer_class, address)
	if endpoint.is_empty():
		return
	_add_row(endpoint)
	_update_counter()


func _on_endpoint_removed(peer_class: StringName, address: String) -> void:
	var key := _key(peer_class, address)
	var row: ConnectBrowserRow = _rows.get(key)
	if row != null:
		row.queue_free()
		_rows.erase(key)
	_mine.erase(key)
	if _has_selection() and _key(_selected_peer_class, _selected_address) == key:
		_clear_selection()
	_update_counter()


func _on_endpoint_updated(peer_class: StringName, address: String) -> void:
	var key := _key(peer_class, address)
	var row: ConnectBrowserRow = _rows.get(key)
	var endpoint := _connection.endpoint(peer_class, address)
	if row != null:
		row.set_endpoint(endpoint)
	if _has_selection() and _key(_selected_peer_class, _selected_address) == key:
		_update_details()


func _update_counter() -> void:
	var total := _rows.size()
	_empty_state.visible = total == 0


func _on_row_selected(
		peer_class: StringName,
		address: String,
		row: ConnectBrowserRow,
) -> void:
	if _selected_row and is_instance_valid(_selected_row):
		_selected_row.button_pressed = false
	_selected_row = row
	_selected_peer_class = peer_class
	_selected_address = address
	_update_details()


func _clear_selection() -> void:
	_selected_row = null
	_selected_peer_class = &""
	_selected_address = ""
	_update_details()


func _update_details() -> void:
	for child in _details_container.get_children():
		child.queue_free()

	if _selected_row == null or not _has_selection():
		_details_header.visible = false
		_details_footer.visible = false
		_details_status_dot.bind_result({ })
		var empty_lbl := Label.new()
		empty_lbl.text = "Select a server to see details"
		empty_lbl.add_theme_color_override("font_color", Color(0.5, 0.5, 0.5))
		_details_container.add_child(empty_lbl)
		return

	var endpoint := _connection.endpoint(_selected_peer_class, _selected_address)
	var r := _endpoint_result(endpoint)
	var is_mine := _mine.has(_key(_selected_peer_class, _selected_address))
	var unavailable := not _is_available(endpoint)

	_details_header.visible = true
	_details_footer.visible = true
	_details_edit_button.disabled = not is_mine
	_details_remove_button.disabled = not is_mine
	_details_join_button.disabled = unavailable
	_details_name_label.text = _selected_row._display_name()
	_details_badge_label.text = ConnectBrowser.format_peer_class_label(
		_selected_peer_class,
	)

	if unavailable:
		_details_status_dot.bind_unavailable()
	else:
		_details_status_dot.bind_result(r)

	_details_container.add_child(
		_create_detail_item(
			"Address",
			ConnectBrowser.format_address(endpoint),
		),
	)
	_details_container.add_child(
		_create_detail_item(
			"Status",
			"Unavailable" if unavailable else _status_text(r),
		),
	)
	var measured: NetwServerInfo = r.get("info", null)
	var latency := measured.latency_ms if measured else -1
	if r.is_empty() or latency >= 0:
		_details_container.add_child(
			_create_detail_item(
				"Latency",
				"%d ms" % latency if latency >= 0 else "-",
			),
		)
	_details_container.add_child(
		_create_detail_item("Players", _players_text(r)),
	)


func _endpoint_result(endpoint: Dictionary) -> Dictionary:
	if endpoint.is_empty() or not bool(endpoint.get("is_observed", false)):
		return { }
	return {
		error = int(endpoint.get("status", FAILED)),
		info = endpoint.get("info"),
	}


func _create_detail_item(
		title: String,
		value: String,
) -> DetailItem:
	var item := _DETAIL_ITEM_SCENE.instantiate() as DetailItem
	item.name = title.to_camel_case() + "Detail"
	item.set_detail(title, value)
	return item


func _on_row_context_requested(
		peer_class: StringName,
		address: String,
		row: ConnectBrowserRow,
		screen_position: Vector2,
) -> void:
	if row == null or peer_class.is_empty():
		return
	_on_row_selected(peer_class, address, row)
	row.button_pressed = true
	_row_menu.show_for_target(_mine.has(_key(peer_class, address)), screen_position)


func _on_row_menu_id_pressed(id: int) -> void:
	match id:
		_ROW_MENU_JOIN:
			_open_join_for_selected()
		_ROW_MENU_EDIT:
			_open_edit_for_selected()
		_ROW_MENU_REMOVE:
			_remove_selected()


func _on_row_activated(
		peer_class: StringName,
		address: String,
		row: ConnectBrowserRow,
) -> void:
	if row == null or peer_class.is_empty():
		return
	_on_row_selected(peer_class, address, row)
	row.button_pressed = true
	_open_join_for_selected()


func _on_add_pressed() -> void:
	_add_popup.open_add(_connection)


func _on_join_direct_pressed() -> void:
	_join_direct_popup.open_join_direct(_connection, _last_username)


func _on_refresh_pressed() -> void:
	if _connection != null:
		_connection.endpoint_refresh()


func _on_host_pressed() -> void:
	if _connection == null:
		return
	_host_popup.open_host(_connection, _last_username)


func _open_join_for_selected() -> void:
	if _selected_row == null or not _has_selection():
		return
	_join_popup.open_join(
		_connection,
		_last_username,
		_selected_peer_class,
		_authored_settings(_selected_peer_class, _selected_address),
	)


func _open_edit_for_selected() -> void:
	if not _mine.has(_key(_selected_peer_class, _selected_address)):
		return
	_add_popup.open_edit(
		_connection,
		_selected_peer_class,
		_selected_address,
		_authored_settings(_selected_peer_class, _selected_address),
	)


# A bookmark the player never gave settings to has none, which is not the same
# as one whose settings are the defaults: an absent entry follows whatever the
# transport's defaults become, and a saved one holds what was typed.
func _authored_settings(
		peer_class: StringName,
		address: String,
) -> Dictionary:
	var record: Dictionary = _mine.get(_key(peer_class, address), { })
	return record.get("settings", { })


func _remove_selected() -> void:
	if not _mine.has(_key(_selected_peer_class, _selected_address)):
		return
	_forget_record(_selected_peer_class, _selected_address)


# An edit that only renames keeps the row and its probe evidence, because the
# session already answers for that endpoint. A changed endpoint is a different
# row, so the old one is forgotten and a new one is bookmarked.
func _on_endpoint_submitted(
		peer_class: StringName,
		address: String,
		display_name: String,
		settings: Dictionary,
		editing_peer_class: StringName,
		editing_address: String,
) -> void:
	var editing_key := (
			_key(editing_peer_class, editing_address)
			if not editing_peer_class.is_empty() else ""
	)
	var edited: Dictionary = (
			_mine.get(editing_key, { }) if editing_key != "" else { }
	)
	if not edited.is_empty():
		if edited.peer_class == peer_class and edited.address == address:
			_connection.endpoint_set_display_name(
				peer_class,
				address,
				display_name,
			)
			edited.display_name = display_name
			edited.settings = settings
			_save_records()
			_update_details()
			return
		_forget_record(editing_peer_class, editing_address)

	var record := {
		peer_class = peer_class,
		address = address,
		display_name = display_name,
		settings = settings,
	}
	_records.append(record)
	_publish_record(record)
	_save_records()
	_clear_selection()
	_connection.endpoint_refresh()


func _on_host_submitted(
		peer_class: StringName,
		settings: Dictionary,
		info: NetwServerInfo,
		username: StringName,
		join_args: Array,
) -> void:
	_hide_banner()
	_last_username = String(username)
	if _session != null:
		_session.set_server_info(info)
	_begin_setup(
		peer_class,
		NetwMultiplayer.TRANSPORT_MODE_HOST,
		"",
		settings,
		username,
		join_args,
	)


func _on_join_submitted(
		settings: Dictionary,
		username: StringName,
		join_args: Array,
) -> void:
	if not _has_selection():
		return
	_join_with_preflight(
		_selected_peer_class,
		_selected_address,
		settings,
		username,
		join_args,
	)


# A direct join is a one-shot endpoint, not a bookmark, so it is never
# recorded and the file is not rewritten.
func _on_join_direct_submitted(
		peer_class: StringName,
		address: String,
		_display_name: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array,
) -> void:
	_join_with_preflight(peer_class, address, settings, username, join_args)


# Entering is what the player pressed Join or Host for, so the setup is over
# and the peer stops being this browser's to withdraw. A cancel after this
# point is a cancel of nothing, which is what keeps a closing browser from
# ending a live match.
func _on_session_entered() -> void:
	_ticket = RID()
	_assigned = null
	_hide_connecting_overlay()
	_hide_banner()
	_show_room(_hosted_room())
	if hide_when_session_active:
		if _room_bar.visible:
			$VBox.visible = false
		else:
			hide()


func _on_session_left() -> void:
	_hide_connecting_overlay()
	_show_room("")
	if hide_when_session_active:
		$VBox.visible = true
		show()


# The fragment is the only part of a URL a page rewrites without reloading.
# Calling replaceState rather than an assignment keeps the back button
# working without pushing a history entry that returns to an ended room.
func _write_url_fragment(room: String) -> void:
	if not OS.has_feature("web"):
		return
	var window: JavaScriptObject = JavaScriptBridge.get_interface("window")
	if window == null:
		return
	var url: JavaScriptObject = JavaScriptBridge.create_object(
		"URL",
		window.location.href,
	)
	url.hash = "" if room.is_empty() else "#" + room
	if String(window.location.href) == String(url.href):
		return
	var history: JavaScriptObject = window.history
	if history != null:
		history.replaceState(null, "", url.href)


## The room code the page URL names, empty when it names none.
##
## A link a host shared carries the code as its fragment, so a player opening
## it arrives with the room already chosen and the join form filled in.
func url_room() -> String:
	if not use_url_fragment or not OS.has_feature("web"):
		return ""
	var window: JavaScriptObject = JavaScriptBridge.get_interface("window")
	if window == null:
		return ""
	return String(window.location.hash).trim_prefix("#").strip_edges()


# A link names a room and nothing else, so the browser opens the direct join
# form on it rather than dialling: the player still owes a name, and a form
# they can cancel is what makes an unreachable room recoverable.
func _offer_url_room() -> void:
	var room := url_room()
	if room.is_empty() or _connection == null:
		return
	var peer_class := _rendezvous_peer_class()
	if peer_class.is_empty():
		return
	_join_direct_popup.open_join_direct(_connection, _last_username)
	_join_direct_popup.preset(peer_class, room)


# The installed transport that takes a room code rather than an address. A
# link carries no transport, so the browser picks the one whose host settings
# declare a signaling namespace.
func _rendezvous_peer_class() -> StringName:
	for entry in _connection.transports():
		var settings: Dictionary = entry.get("host_settings", { })
		if settings.has("signaling_namespace"):
			return StringName(entry.get("peer_class", &""))
	return &""


# The identifier a joining player needs, which only the host can read: a
# client already knows the address it dialled, and a transport that needs no
# rendezvous answers with nothing. Every transport answers in its own terms,
# an ENet host with its address and a signalled one with its room code.
func _hosted_room() -> String:
	if _connection == null or _api == null or not _api.is_server():
		return ""
	return _connection.join_address


# What the host is looking at is what a joiner would type, so the bar borrows
# the label the join form puts over that same field.
func _hosted_room_label() -> String:
	if _connection == null or _active_peer_class.is_empty():
		return "Room"
	for entry in _connection.transports():
		if StringName(entry.get("peer_class", &"")) == _active_peer_class:
			return String(entry.get("address_label", "Room"))
	return "Room"


func _show_room(room: String) -> void:
	if _room_bar == null or _room_code_edit == null:
		return
	_room_code_edit.text = room
	_room_label.text = _hosted_room_label()
	_room_bar.visible = not room.is_empty()
	if use_url_fragment:
		_write_url_fragment(room)


func _on_room_copy_pressed() -> void:
	var room := _room_code_edit.text
	if room.is_empty():
		return
	DisplayServer.clipboard_set(_shareable(room))
	_room_copy_button.text = "Copied"
	await get_tree().create_timer(1.5).timeout
	if is_instance_valid(_room_copy_button):
		_room_copy_button.text = "Copy"


# On the web a room is reachable as a link, so that is what a player wants on
# their clipboard. Everywhere else the code is the whole of it.
func _shareable(room: String) -> String:
	if not use_url_fragment or not OS.has_feature("web"):
		return room
	var window: JavaScriptObject = JavaScriptBridge.get_interface("window")
	if window == null:
		return room
	var url: JavaScriptObject = JavaScriptBridge.create_object(
		"URL",
		window.location.href,
	)
	url.hash = "#" + room
	return String(url.href)


func _show_connecting_overlay(peer_class: StringName, address: String) -> void:
	_connecting_popup.open_connecting(_connection, peer_class, address)
	$VBox.modulate.a = 0.5


func _hide_connecting_overlay() -> void:
	_connecting_popup.hide()
	$VBox.modulate.a = 1.0


func _on_popup_cancelled() -> void:
	_cancel_setup()


func _join_with_preflight(
		peer_class: StringName,
		address: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array,
) -> void:
	_hide_banner()
	_last_username = String(username)
	if not _transport_is_available(peer_class):
		_show_banner("This transport is not available on this platform.")
		return
	var endpoint := _connection.endpoint(peer_class, address)
	if not endpoint.is_empty() and not _is_available(endpoint):
		_show_banner("This endpoint is not available right now.")
		return
	_begin_setup(
		peer_class,
		NetwMultiplayer.TRANSPORT_MODE_CLIENT,
		address,
		settings,
		username,
		join_args,
	)


func _show_banner(reason: String) -> void:
	if _banner_label == null or _banner == null:
		push_warning("ConnectBrowser: %s" % reason)
		return
	_banner_label.text = reason
	_banner.visible = true


func _hide_banner() -> void:
	if _banner != null:
		_banner.visible = false


func _status_text(result: Dictionary) -> String:
	if result.is_empty():
		return "..."
	match int(result.get("error", FAILED)):
		OK:
			return "OK"
		ERR_BUSY:
			return "BUSY"
		ERR_CANT_CONNECT:
			return "UNREACHABLE"
		ERR_TIMEOUT:
			return "TIMEOUT"
		ERR_UNAVAILABLE:
			return "UNSUPPORTED"
		ERR_UNAUTHORIZED:
			return "INCOMPATIBLE"
		_:
			return "ERROR"


func _players_text(result: Dictionary) -> String:
	var info: NetwServerInfo = result.get("info", null)
	if info == null:
		return "-"
	return "%d/%d" % [info.players, info.max_players]


func _on_details_edit_pressed() -> void:
	_open_edit_for_selected()


func _on_details_remove_pressed() -> void:
	_remove_selected()


func _on_details_join_pressed() -> void:
	_open_join_for_selected()


## Human-readable label for a [param peer_class], such as
## [code]&"ENetMultiplayerPeer"[/code] -> "ENet". "-" when empty.
static func format_peer_class_label(peer_class: StringName) -> String:
	var text := String(peer_class)
	if text.is_empty():
		return "-"
	return text.trim_suffix("MultiplayerPeer")


## Displayable address for an [param endpoint] snapshot. Either its
## explicit address or "-" when it relies on a transport's own local default.
static func format_address(endpoint: Dictionary) -> String:
	if endpoint.is_empty():
		return "-"
	var address := String(endpoint.get("address", "")).strip_edges()
	return address if not address.is_empty() else "-"


## Builds a [Control] typed by [param value]'s [Variant] type, seeded with
## [param value]. [param field_name] selects a dedicated editor when a setting
## has more structure than its type describes. Shared by the Host settings
## form and any generic Join form drawn from
## [method NetwConnectHandle.join_schema].
static func make_value_control(
		value: Variant,
		field_name: StringName = &"",
) -> Control:
	match typeof(value):
		TYPE_BOOL:
			var box := CheckBox.new()
			box.button_pressed = value
			return box
		TYPE_INT:
			var spin := SpinBox.new()
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.step = 1
			spin.value = value
			return spin
		TYPE_FLOAT:
			var spin := SpinBox.new()
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.step = 0.01
			spin.value = value
			return spin
		TYPE_PACKED_STRING_ARRAY:
			var listed := _STRING_LIST_EDIT.instantiate()
			listed.set_value(value)
			return listed
		TYPE_ARRAY:
			if field_name == &"ice_servers":
				var ice_servers := _ICE_SERVERS_EDIT.instantiate()
				ice_servers.set_value(value)
				return ice_servers
			var structured := LineEdit.new()
			structured.text = JSON.stringify(value)
			return structured
		_:
			var edit := LineEdit.new()
			edit.text = str(value)
			return edit


## Reads back [param control]'s value, cast to [param value_type].
##
## A type absent from this match reads back as the [String] a [LineEdit]
## holds, and every consumer that type-checks its settings then discards it,
## so the field draws as editable and does nothing. A case here and a case in
## [method make_value_control] are added together.
static func value_from_control(control: Control, value_type: int) -> Variant:
	match value_type:
		TYPE_BOOL:
			return (control as CheckBox).button_pressed
		TYPE_INT:
			return int((control as SpinBox).value)
		TYPE_FLOAT:
			return (control as SpinBox).value
		TYPE_STRING_NAME:
			return StringName((control as LineEdit).text)
		TYPE_NODE_PATH:
			return NodePath((control as LineEdit).text)
		TYPE_PACKED_STRING_ARRAY:
			if control.has_method(&"get_value"):
				return control.call(&"get_value")
			var entries := PackedStringArray()
			for piece in (control as LineEdit).text.split(",", false):
				var trimmed := piece.strip_edges()
				if not trimmed.is_empty():
					entries.append(trimmed)
			return entries
		TYPE_ARRAY:
			if control.has_method(&"get_value"):
				return control.call(&"get_value")
			# JSON.parse_string raises an engine error on malformed input,
			# which every half-typed field is. The instance parser reports
			# the same fault by return value instead.
			var text := (control as LineEdit).text.strip_edges()
			var reader := JSON.new()
			if reader.parse(text) == OK and reader.data is Array:
				return reader.data
			push_warning("A settings field is not a JSON array: %s" % [text])
			return null
		_:
			return (control as LineEdit).text


## Whether a settings entry seeded with [param value] can be drawn as a field
## and read back from it.
##
## An installation seam carries an object its caller supplies in code, and the
## [code]signaler[/code] entry of the WebRTC transport's host settings is one:
## its default is null, so a form that draws it offers a control reading
## [code]<null>[/code] that no typed text can ever satisfy. A form asks this
## before drawing a row rather than rendering a dead control.
static func can_author_value(value: Variant) -> bool:
	match typeof(value):
		TYPE_NIL, TYPE_OBJECT, TYPE_CALLABLE, TYPE_SIGNAL, TYPE_RID:
			return false
		_:
			return true


## The empty value of [param value_type], used to seed a [method
## make_value_control] call when no default is known, as
## [method NetwConnectHandle.join_schema] entries never carry one.
static func zero_value(value_type: int) -> Variant:
	match value_type:
		TYPE_BOOL:
			return false
		TYPE_INT:
			return 0
		TYPE_FLOAT:
			return 0.0
		TYPE_STRING_NAME:
			return StringName()
		_:
			return ""
