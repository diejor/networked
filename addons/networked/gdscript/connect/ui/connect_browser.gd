## A server browser that lists, joins and hosts games for its session.
##
## [method _ready] is the whole wiring, and a game's own connect screen starts
## by copying it.
class_name ConnectBrowser
extends Control

const ROW := preload("res://addons/networked/gdscript/connect/ui/row.tscn")

## Hides the server list while the session is online.
@export var hide_when_session_active := true
## Settings every connection through a transport starts from.
@export var transport_defaults: Array[ConnectTransportConfig] = []
## On the web, keeps the hosted room after [code]#[/code] in the page URL.
@export var use_url_fragment := true

var rows: Dictionary[NetwEndpoint, ConnectBrowserRow] = { }
var selected: NetwEndpoint
var username := "Player"

@onready var session: NetwSession = Netw.session(self)
@onready var connection: NetwConnection = session.connection
@onready var server_list: VBoxContainer = $VBox
@onready var list_box: VBoxContainer = %ListBox
@onready var empty_state: Control = %EmptyState
@onready var banner: Control = %Banner
@onready var banner_label: Label = %BannerLabel
@onready var details: Control = %Details
@onready var details_status_dot: StatusDot = %DetailsStatusDot
@onready var details_name_label: Label = %DetailsNameLabel
@onready var details_source_label: Label = %DetailsSourceLabel
@onready var details_address_label: Label = %DetailsAddressLabel
@onready var details_status_label: Label = %DetailsStatusLabel
@onready var details_ping_label: Label = %DetailsPingLabel
@onready var details_players_label: Label = %DetailsPlayersLabel
@onready var join_button: Button = %DetailsJoinButton
@onready var host_popup: HostPopup = %HostPopup
@onready var join_popup: JoinPopup = %JoinPopup
@onready var join_direct_popup: JoinDirectPopup = %JoinDirectPopup
@onready var connecting_popup: ConnectingPopup = %ConnectingPopup


func _enter_tree() -> void:
	get_viewport().gui_embed_subwindows = true


func _ready() -> void:
	for config in transport_defaults:
		connection.set_defaults(config.peer_class(), config.settings())
	connection.endpoint_added.connect(on_endpoint_added)
	connection.endpoint_removed.connect(on_endpoint_removed)
	connection.state_changed.connect(on_state_changed)
	connection.progressed.connect(on_progressed)
	connection.failed.connect(on_failed, CONNECT_DEFERRED)
	connection.join_address_changed.connect(on_join_address_changed)
	server_list.visibility_changed.connect(on_server_list_visibility_changed)
	for endpoint in connection.endpoints:
		on_endpoint_added(endpoint)
	connection.browsing = server_list.is_visible_in_tree()
	on_state_changed(connection.state)
	on_endpoint_changed()
	banner.hide()

	%HostButton.pressed.connect(on_host_pressed)
	%JoinDirectButton.pressed.connect(on_join_direct_pressed)
	join_button.pressed.connect(on_join_pressed)
	host_popup.submitted.connect(on_host_popup_submitted)
	join_popup.submitted.connect(on_join_popup_submitted)
	join_direct_popup.submitted.connect(on_join_direct_popup_submitted)
	connecting_popup.cancelled.connect(connection.cancel)

	var room := (
			str(JavaScriptBridge.eval("location.hash")).trim_prefix("#")
			if use_url_fragment and OS.has_feature("web") else ""
	)
	if not room.is_empty():
		join_direct_popup.open(
			connection,
			username,
			&"WebRTCMultiplayerPeer",
			room,
		)


func _exit_tree() -> void:
	connection.browsing = false


func on_endpoint_added(endpoint: NetwEndpoint) -> void:
	var row: ConnectBrowserRow = ROW.instantiate()
	row.endpoint = endpoint
	rows[endpoint] = row
	list_box.add_child(row)
	row.selected.connect(on_row_selected.bind(endpoint))
	row.activated.connect(on_row_activated.bind(endpoint))
	endpoint.changed.connect(on_endpoint_changed)
	empty_state.hide()


func on_endpoint_removed(endpoint: NetwEndpoint) -> void:
	rows[endpoint].queue_free()
	rows.erase(endpoint)
	empty_state.visible = rows.is_empty()
	if endpoint == selected:
		on_row_selected(null)


func on_state_changed(state: NetwConnection.State) -> void:
	match state:
		NetwConnection.STATE_OFFLINE:
			connecting_popup.hide()
			server_list.show()
		NetwConnection.STATE_CREATING, NetwConnection.STATE_CONNECTING:
			banner.hide()
			connecting_popup.open()
		NetwConnection.STATE_ONLINE:
			connecting_popup.hide()
			server_list.visible = not hide_when_session_active


func on_progressed(message: String, ratio: float) -> void:
	connecting_popup.progressed(message, ratio)


func on_failed(_error: Error, reason: String) -> void:
	banner_label.text = reason
	banner.show()
	connecting_popup.failed(reason)


func on_join_address_changed(address: String) -> void:
	if use_url_fragment and OS.has_feature("web"):
		JavaScriptBridge.get_interface("history").replaceState(
			null,
			"",
			"#" + address,
		)


func on_server_list_visibility_changed() -> void:
	connection.browsing = server_list.is_visible_in_tree()


func join(
		endpoint: NetwEndpoint,
		settings: Dictionary,
		join_args: Array,
) -> void:
	Netw.prepare_join.callv([self, username] + join_args)
	connection.join(endpoint, settings)


func host(
		transport: StringName,
		settings: Dictionary,
		info: NetwServerInfo,
		join_args: Array,
) -> void:
	session.set_server_info(info)
	Netw.prepare_join.callv([self, username] + join_args)
	connection.host(transport, settings)


func on_row_selected(endpoint: NetwEndpoint) -> void:
	selected = endpoint
	on_endpoint_changed()


func on_row_activated(endpoint: NetwEndpoint) -> void:
	on_row_selected(endpoint)
	on_join_pressed()


func on_endpoint_changed() -> void:
	details.visible = selected != null
	if selected == null:
		return
	var row := rows[selected]
	details_status_dot.status = selected.status
	details_name_label.text = row.name_label.text
	details_source_label.text = row.badge_label.text
	details_address_label.text = selected.address
	details_status_label.text = StatusDot.WORDS[selected.status]
	details_ping_label.text = row.ping_label.text
	details_players_label.text = row.players_label.text
	join_button.disabled = selected.status in [
		NetwEndpoint.STATUS_FULL,
		NetwEndpoint.STATUS_INCOMPATIBLE,
		NetwEndpoint.STATUS_UNSUPPORTED,
	]


func on_join_pressed() -> void:
	join_popup.open(connection, selected, username)


func on_join_direct_pressed() -> void:
	join_direct_popup.open(connection, username)


func on_host_pressed() -> void:
	host_popup.open(connection, username)


func on_join_popup_submitted(
		settings: Dictionary,
		player_name: String,
		join_args: Array,
) -> void:
	username = player_name
	join(selected, settings, join_args)


func on_join_direct_popup_submitted(
		transport: StringName,
		address: String,
		settings: Dictionary,
		player_name: String,
		join_args: Array,
) -> void:
	username = player_name
	join(connection.add(transport, address), settings, join_args)


func on_host_popup_submitted(
		transport: StringName,
		settings: Dictionary,
		info: NetwServerInfo,
		player_name: String,
		join_args: Array,
) -> void:
	username = player_name
	host(transport, settings, info, join_args)
