## A color-coded circular status dot indicating server health and probing.
class_name StatusDot
extends Panel


var _pulse_tween: Tween


## Binds this status dot directly to a [ServerInfoResult] or sets it as
## pending (probing) if [param result] is [code]null[/code].
func bind_result(result: ServerInfoResult) -> void:
	if result == null:
		_update_status_style(Color(0.6, 0.6, 0.6))
		_start_pulse_tween()
		return

	_stop_pulse_tween()
	match result.status:
		ServerInfoResult.Status.OK:
			_update_status_style(Color(0.24, 0.81, 0.44))
		ServerInfoResult.Status.BUSY:
			_update_status_style(Color(0.95, 0.77, 0.06))
		ServerInfoResult.Status.UNREACHABLE, ServerInfoResult.Status.TIMEOUT:
			_update_status_style(Color(0.91, 0.3, 0.24))
		ServerInfoResult.Status.UNSUPPORTED:
			var info := result.info
			_update_status_style(
				Color(0.24, 0.81, 0.44) if info
				else Color(0.95, 0.77, 0.06)
			)
		_:
			_update_status_style(Color(0.91, 0.3, 0.24))


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
		self, "self_modulate:a", 0.3, 0.6
	).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	_pulse_tween.tween_property(
		self, "self_modulate:a", 1.0, 0.6
	).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)


func _stop_pulse_tween() -> void:
	if _pulse_tween != null:
		_pulse_tween.kill()
		_pulse_tween = null
	self_modulate.a = 1.0
