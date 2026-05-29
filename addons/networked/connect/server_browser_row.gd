## One row in the server browser. Renders a [JoinTarget] and the
## latest [ServerInfoResult] for it (if any).
##
## Emits [signal pressed] when the user clicks the row so the browser
## can update its selection.
@tool
class_name ServerBrowserRow
extends Button


## Emitted when the user clicks the row. The browser uses this to
## update the details panel.
signal selected(target: JoinTarget)


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


## Populates the row from [param p_target] and re-renders.
func bind_target(p_target: JoinTarget) -> void:
	target = p_target
	_refresh()


## Updates the row with a fresh probe result (direct rows) or live
## lobby info (provider rows). Pass [code]null[/code] to clear the
## status cells (probe in flight).
func set_result(p_result: ServerInfoResult) -> void:
	result = p_result
	_refresh()


func _on_pressed() -> void:
	selected.emit(target)


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

	_name_label.text = target.display_name
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
