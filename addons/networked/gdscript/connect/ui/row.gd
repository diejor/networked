## One row in the [ConnectBrowser]. Renders a browse endpoint read through a
## [NetwConnectHandle] and its latest snapshot [Dictionary].
class_name ConnectBrowserRow
extends PanelContainer

signal selected(peer_class: StringName, address: String)
signal context_requested(
		peer_class: StringName,
		address: String,
		row: ConnectBrowserRow,
		screen_position: Vector2,
)
signal activated(peer_class: StringName, address: String, row: ConnectBrowserRow)

var peer_class: StringName = &""
var address: String = ""
var endpoint: Dictionary = { }
var _connection: NetwConnectHandle

@onready var _name_label: Label = %NameLabel
@onready var _badge_label: Label = %BadgeLabel
@onready var _players_label: Label = %PlayersLabel
@onready var _ping_label: Label = %PingLabel
@onready var _status_dot: StatusDot = %StatusDot
@onready var _address_label: Label = %AddressLabel
@onready var _row_button: Button = %ConnectButton

var button_pressed: bool = false:
	set(value):
		button_pressed = value
		if _row_button != null:
			_row_button.button_pressed = value


func _ready() -> void:
	_row_button.pressed.connect(_on_pressed)
	_row_button.gui_input.connect(_on_row_button_gui_input)
	_refresh()


func bind_endpoint(
		handle: NetwConnectHandle,
		p_peer_class: StringName,
		p_address: String,
) -> void:
	_connection = handle
	peer_class = p_peer_class
	address = p_address
	_refresh()


func set_endpoint(p_endpoint: Dictionary) -> void:
	endpoint = p_endpoint
	_refresh()


func _on_pressed() -> void:
	selected.emit(peer_class, address)


func _on_row_button_gui_input(event: InputEvent) -> void:
	var mouse_event := event as InputEventMouseButton
	if mouse_event == null:
		return
	if (
			mouse_event.button_index == MOUSE_BUTTON_LEFT
			and mouse_event.pressed
	):
		_row_button.accept_event()
		if mouse_event.double_click:
			activated.emit(peer_class, address, self)
		else:
			selected.emit(peer_class, address)
		return
	if mouse_event.button_index != MOUSE_BUTTON_RIGHT:
		return
	if not mouse_event.pressed:
		return
	_row_button.accept_event()
	context_requested.emit(
		peer_class,
		address,
		self,
		_row_button.get_screen_position() + mouse_event.position,
	)


func _refresh() -> void:
	if _name_label == null:
		return
	if _connection == null or peer_class.is_empty():
		_name_label.text = ""
		_badge_label.text = ""
		_address_label.text = ""
		_players_label.text = ""
		_ping_label.text = ""
		return

	_name_label.text = _display_name()
	_address_label.text = ConnectBrowser.format_address(endpoint)
	_badge_label.text = ConnectBrowser.format_peer_class_label(peer_class)
	_render_metrics()


func _result() -> Dictionary:
	if endpoint.is_empty() or not bool(endpoint.get("is_observed", false)):
		return { }
	return {
		error = int(endpoint.get("status", FAILED)),
		info = endpoint.get("info"),
	}


func _render_metrics() -> void:
	var result := _result()
	if result.is_empty():
		_players_label.text = "-"
		_ping_label.text = "-"
		_status_dot.bind_result(result)
		return

	var info: NetwServerInfo = result.get("info", null)
	var occupancy := (
			"%d/%d" % [info.players, info.max_players] if info else "-"
	)
	match int(result.get("error", FAILED)):
		OK:
			_players_label.text = occupancy
			_ping_label.text = (
					"%d ms" % info.latency_ms
					if info and info.latency_ms >= 0 else "."
			)
		ERR_BUSY:
			_players_label.text = "FULL"
			_ping_label.text = "-"
		ERR_UNAVAILABLE:
			_players_label.text = occupancy
			_ping_label.text = "."
		ERR_UNAUTHORIZED:
			_players_label.text = occupancy
			_ping_label.text = "x"
		_:
			_players_label.text = "-"
			_ping_label.text = "-"

	_status_dot.bind_result(result)


func _display_name() -> String:
	var display_name := String(endpoint.get("display_name", ""))
	if not display_name.strip_edges().is_empty():
		return display_name
	return ConnectBrowser.format_address(endpoint)
