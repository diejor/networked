## Passive always-on-top pinning for the Networked debugger.
##
## Added as a child of [NetworkedDebugReporter]. The editor decides which
## windows get pinned; this node only flips the [code]ALWAYS_ON_TOP[/code]
## flag, raises the window, and optionally re-applies a stored geometry
## (position + size) on pin.
##
## Reports changed window geometry back to the editor on focus loss, close, and
## shutdown so the editor can re-apply it next time the window is pinned.
extends Node

class_name NetWindowPin

const SETTING_PIN_ENABLED = "networked/debug/auto_pin_enabled"

signal geometry_reported(rect: Rect2i)

var _pinned: bool = false
var _enforce_timer: Timer
var _has_reported_rect: bool = false
var _last_reported_rect: Rect2i
var _dbg: NetwHandle = Netw.dbg.handle(self)


func _enter_tree() -> void:
	if not _allowed():
		return
	_enforce_timer = Timer.new()
	_enforce_timer.wait_time = 2.0
	_enforce_timer.autostart = false
	_enforce_timer.timeout.connect(_enforce_state)
	add_child(_enforce_timer)

	var win := get_window()
	# Capture geometry before shutdown paths race the EngineDebugger channel.
	win.close_requested.connect(_report_geometry)
	win.focus_exited.connect(_report_geometry)


func _exit_tree() -> void:
	if _pinned:
		_report_geometry()


func _allowed() -> bool:
	if not OS.has_feature("debug") or DisplayServer.get_name() == "headless" \
			or OS.has_feature("web"):
		return false
	if not ProjectSettings.get_setting(SETTING_PIN_ENABLED, true):
		return false
	return true


## Pins this window. If [param rect] is a [Rect2i], applies that geometry
## before pinning. Pass [code]null[/code] to pin in place.
func pin(rect: Variant) -> void:
	if not _allowed():
		_dbg.trace("WindowPin: [PinIgnored] not allowed")
		return
	_pinned = true

	var win := get_window()
	if not is_instance_valid(win):
		_dbg.trace("WindowPin: [PinIgnored] missing window")
		return

	if Engine.has_method("is_embedded_in_editor") and \
			Engine.is_embedded_in_editor():
		_dbg.trace("WindowPin: [PinIgnored] embedded in editor")
		return

	var win_id := win.get_window_id()
	if rect is Rect2i:
		var r: Rect2i = rect
		if r.size.x > 0 and r.size.y > 0:
			DisplayServer.window_set_mode(
				DisplayServer.WINDOW_MODE_WINDOWED,
				win_id,
			)
			DisplayServer.window_set_position(r.position, win_id)
			DisplayServer.window_set_size(r.size, win_id)

	var on_top := DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP
	DisplayServer.window_set_flag(on_top, true, win_id)
	DisplayServer.window_move_to_foreground(win_id)
	_dbg.trace("WindowPin: [Pinned] rect=%s win_id=%d", [str(rect), win_id])

	if _enforce_timer and _enforce_timer.is_stopped():
		_enforce_timer.start()


## Drops always-on-top and stops enforcement.
##
## Geometry is preserved in-place. It is reported by focus loss, close, or
## shutdown paths while the window is still pinned.
func unpin() -> void:
	if not _pinned:
		_dbg.trace("WindowPin: [UnpinIgnored] not pinned")
		return
	_pinned = false
	if _enforce_timer:
		_enforce_timer.stop()

	var win := get_window()
	if not is_instance_valid(win):
		return
	var win_id := win.get_window_id()
	var on_top := DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP
	if DisplayServer.window_get_flag(on_top, win_id):
		DisplayServer.window_set_flag(on_top, false, win_id)
	_dbg.trace("WindowPin: [Unpinned] win_id=%d", [win_id])


func _enforce_state() -> void:
	if not _pinned:
		return
	var win := get_window()
	if not is_instance_valid(win):
		return
	var win_id := win.get_window_id()
	var on_top := DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP
	if not DisplayServer.window_get_flag(on_top, win_id):
		DisplayServer.window_set_flag(on_top, true, win_id)


func _report_geometry() -> void:
	if not _pinned:
		return
	var win := get_window()
	if not is_instance_valid(win):
		return
	if not EngineDebugger.is_active():
		return
	var win_id := win.get_window_id()
	var pos := DisplayServer.window_get_position(win_id)
	var size := DisplayServer.window_get_size(win_id)
	if size.x <= 0 or size.y <= 0:
		return
	var rect := Rect2i(pos, size)
	if _has_reported_rect and rect == _last_reported_rect:
		return
	_has_reported_rect = true
	_last_reported_rect = rect
	geometry_reported.emit(rect)
