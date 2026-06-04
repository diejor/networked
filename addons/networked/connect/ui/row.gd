## One row in the [ConnectBrowser]. Renders a [JoinTarget] and its
## latest [ServerInfoResult].
class_name ConnectBrowserRow
extends PanelContainer

signal selected(target: JoinTarget)
signal context_requested(
		target: JoinTarget,
		row: ConnectBrowserRow,
		screen_position: Vector2,
)
signal activated(target: JoinTarget, row: ConnectBrowserRow)

var target: JoinTarget
var result: ServerInfoResult

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


func bind_target(p_target: JoinTarget) -> void:
	target = p_target
	_refresh()


func set_result(p_result: ServerInfoResult) -> void:
	result = p_result
	_refresh()


func _on_pressed() -> void:
	selected.emit(target)


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
			activated.emit(target, self)
		else:
			selected.emit(target)
		return
	if mouse_event.button_index != MOUSE_BUTTON_RIGHT:
		return
	if not mouse_event.pressed:
		return
	_row_button.accept_event()
	context_requested.emit(
		target,
		self,
		_row_button.get_screen_position() + mouse_event.position,
	)


func _refresh() -> void:
	if _name_label == null:
		return
	if target == null:
		_name_label.text = ""
		_badge_label.text = ""
		_address_label.text = ""
		_players_label.text = ""
		_ping_label.text = ""
		return

	_name_label.text = _display_name()
	_address_label.text = ConnectUiShared.format_address(target)

	var backend_label := "unknown"
	if target.backend != null:
		backend_label = ConnectUiShared.format_backend_label(target.backend)
	_badge_label.text = backend_label

	if target.backend != null and not target.backend.is_available():
		_render_unavailable()
		return
	_render_metrics()


func _render_unavailable() -> void:
	_players_label.text = "-"
	_ping_label.text = "-"
	_status_dot.bind_unavailable()


func _render_metrics() -> void:
	if result == null:
		_players_label.text = "-"
		_ping_label.text = "-"
		_status_dot.bind_result(null)
		return

	match result.status:
		ServerInfoResult.Status.OK:
			var info := result.info
			_players_label.text = (
					"%d/%d" % [info.players, info.max_players]
					if info else "-"
			)
			_ping_label.text = (
					"%d ms" % result.latency_ms
					if result.latency_ms >= 0 else "."
			)
		ServerInfoResult.Status.BUSY:
			_players_label.text = "FULL"
			_ping_label.text = "-"
		ServerInfoResult.Status.UNREACHABLE:
			_players_label.text = "-"
			_ping_label.text = "-"
		ServerInfoResult.Status.TIMEOUT:
			_players_label.text = "-"
			_ping_label.text = "-"
		ServerInfoResult.Status.UNSUPPORTED:
			var info := result.info
			_players_label.text = (
					"%d/%d" % [info.players, info.max_players]
					if info else "-"
			)
			_ping_label.text = "."
		ServerInfoResult.Status.INCOMPATIBLE:
			var info := result.info
			_players_label.text = (
					"%d/%d" % [info.players, info.max_players]
					if info else "-"
			)
			_ping_label.text = "x"
		_:
			_players_label.text = "-"
			_ping_label.text = "-"

	_status_dot.bind_result(result)


func _display_name() -> String:
	if not target.display_name.strip_edges().is_empty():
		return target.display_name
	return ConnectUiShared.format_address(target)
