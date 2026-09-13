## A color-coded circular status dot indicating server health and probing.
class_name StatusDot
extends Panel

var _pulse_tween: Tween


## Binds this status dot to one probe answer for a [ConnectBrowser] row.
##
## [param result] is a [code]{ error, info }[/code] pair built from the
## [code]status[/code] and [code]info[/code] keys of the snapshot
## [method NetwConnectHandle.endpoint] answers, and an empty [Dictionary] is a row
## nobody has probed yet, which shows as pending rather than as a failure.
func bind_result(result: Dictionary) -> void:
	if result.is_empty():
		tooltip_text = "Checking"
		_update_status_style(Color(0.6, 0.6, 0.6))
		_start_pulse_tween()
		return

	_stop_pulse_tween()
	var info: NetwServerInfo = result.get("info", null)
	match int(result.get("error", FAILED)):
		OK:
			tooltip_text = "OK"
			_update_status_style(Color(0.24, 0.81, 0.44))
		ERR_BUSY:
			tooltip_text = "Busy"
			_update_status_style(Color(0.95, 0.77, 0.06))
		ERR_CANT_CONNECT, ERR_TIMEOUT:
			tooltip_text = "Unreachable"
			_update_status_style(Color(0.91, 0.3, 0.24))
		ERR_UNAVAILABLE:
			tooltip_text = "Unsupported"
			_update_status_style(
				Color(0.24, 0.81, 0.44) if info else Color(0.95, 0.77, 0.06),
			)
		ERR_UNAUTHORIZED:
			tooltip_text = "Incompatible game build"
			_update_status_style(Color(0.6, 0.35, 0.85))
		_:
			tooltip_text = "Error"
			_update_status_style(Color(0.91, 0.3, 0.24))


## Marks this dot as a transport that cannot run on the current platform.
##
## Availability is distinct from a probe result, so this never routes through
## [method bind_result].
func bind_unavailable() -> void:
	_stop_pulse_tween()
	tooltip_text = "Not available on this platform"
	_update_status_style(Color(0.45, 0.45, 0.5))


func _update_status_style(color: Color) -> void:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(4)
	add_theme_stylebox_override("panel", style)
	self_modulate.a = 1.0


func _start_pulse_tween() -> void:
	if _pulse_tween != null and _pulse_tween.is_valid():
		return
	_pulse_tween = create_tween().set_loops()
	_pulse_tween.tween_property(
		self,
		"self_modulate:a",
		0.3,
		0.6,
	).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	_pulse_tween.tween_property(
		self,
		"self_modulate:a",
		1.0,
		0.6,
	).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)


func _stop_pulse_tween() -> void:
	if _pulse_tween != null:
		_pulse_tween.kill()
		_pulse_tween = null
	self_modulate.a = 1.0
