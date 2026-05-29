## One row in the [ConnectBrowser]. Renders a [JoinTarget] and its
## latest [ServerInfoResult]. Selection and context signals bubble
## back to the browser.
class_name ConnectBrowserRow
extends Button


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
@onready var _status_label: Label = %StatusLabel


func _ready() -> void:
	pressed.connect(_on_pressed)
	_refresh()


func bind_target(p_target: JoinTarget) -> void:
	target = p_target
	_refresh()


func set_result(p_result: ServerInfoResult) -> void:
	result = p_result
	_refresh()


func _on_pressed() -> void:
	selected.emit(target)


func _gui_input(event: InputEvent) -> void:
	var mouse_event := event as InputEventMouseButton
	if mouse_event == null:
		return
	if (
		mouse_event.button_index == MOUSE_BUTTON_LEFT
		and mouse_event.double_click
		and mouse_event.pressed
	):
		accept_event()
		activated.emit(target, self)
		return
	if mouse_event.button_index != MOUSE_BUTTON_RIGHT:
		return
	if not mouse_event.pressed:
		return
	accept_event()
	context_requested.emit(
		target, self, get_screen_position() + mouse_event.position
	)


func _refresh() -> void:
	if _name_label == null:
		return
	if target == null:
		_name_label.text = ""
		_badge_label.text = ""
		_players_label.text = ""
		_ping_label.text = ""
		_status_label.text = ""
		return

	_name_label.text = _display_name()
	_badge_label.text = "[%s]" % (
		"direct" if target.is_direct() else String(target.provider_id)
	)

	if target.is_direct():
		_render_direct_metrics()
	else:
		_render_provider_metrics()


func _render_direct_metrics() -> void:
	if result == null:
		_players_label.text = "-"
		_ping_label.text = "-"
		_status_label.text = "..."
		return
	match result.status:
		ServerInfoResult.Status.OK:
			var info := result.info
			_players_label.text = (
				"%d/%d" % [info.players, info.max_players]
				if info else "-"
			)
			_ping_label.text = "%d ms" % result.latency_ms
			_status_label.text = "OK"
		ServerInfoResult.Status.BUSY:
			_players_label.text = "FULL"
			_ping_label.text = "-"
			_status_label.text = "BUSY"
		ServerInfoResult.Status.UNREACHABLE:
			_players_label.text = "-"
			_ping_label.text = "-"
			_status_label.text = "UNREACHABLE"
		ServerInfoResult.Status.TIMEOUT:
			_players_label.text = "-"
			_ping_label.text = "-"
			_status_label.text = "TIMEOUT"
		ServerInfoResult.Status.UNSUPPORTED:
			_players_label.text = "-"
			_ping_label.text = "-"
			_status_label.text = "UNSUPPORTED"
		_:
			_players_label.text = "-"
			_ping_label.text = "-"
			_status_label.text = "ERROR"


func _render_provider_metrics() -> void:
	var info := result.info if result else null
	if info:
		_players_label.text = "%d/%d" % [info.players, info.max_players]
	else:
		_players_label.text = "-"
	_ping_label.text = "."
	_status_label.text = "OK" if info else "..."


func _display_name() -> String:
	if not target.display_name.strip_edges().is_empty():
		return target.display_name
	if target.is_direct():
		return ConnectUiShared.format_address(target)
	return "(unnamed)"
