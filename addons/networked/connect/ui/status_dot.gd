## A color-coded circular status dot indicating server health and probing.
class_name StatusDot
extends Panel

var _pulse_tween: Tween


## Binds this status dot directly to a [BackendPeer.ProbeResult] or sets it as
## pending (probing) if [param result] is [code]null[/code].
func bind_result(result: BackendPeer.ProbeResult) -> void:
	if result == null:
		tooltip_text = "Checking"
		_update_status_style(Color(0.6, 0.6, 0.6))
		_start_pulse_tween()
		return

	_stop_pulse_tween()
	match result.status:
		BackendPeer.ProbeResult.Status.OK:
			tooltip_text = "OK"
			_update_status_style(Color(0.24, 0.81, 0.44))
		BackendPeer.ProbeResult.Status.BUSY:
			tooltip_text = "Busy"
			_update_status_style(Color(0.95, 0.77, 0.06))
		BackendPeer.ProbeResult.Status.UNREACHABLE, BackendPeer.ProbeResult.Status.TIMEOUT:
			tooltip_text = "Unreachable"
			_update_status_style(Color(0.91, 0.3, 0.24))
		BackendPeer.ProbeResult.Status.UNSUPPORTED:
			tooltip_text = "Unsupported"
			var info := result.info
			_update_status_style(
				Color(0.24, 0.81, 0.44) if info else Color(0.95, 0.77, 0.06),
			)
		BackendPeer.ProbeResult.Status.INCOMPATIBLE:
			tooltip_text = "Incompatible game build"
			_update_status_style(Color(0.6, 0.35, 0.85))
		_:
			tooltip_text = "Error"
			_update_status_style(Color(0.91, 0.3, 0.24))


## Marks this dot as a transport that cannot run on the current platform.
##
## Availability is distinct from a probe result, so this never routes through
## [method bind_result] or [BackendPeer.ProbeResult].
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
