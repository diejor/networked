## Restores debug reporter and trace state when [method close] is called.
class_name NetwDbgScope
extends RefCounted

var _previous_enabled := false
var _previous_sink: Callable
var _closed := false


func _init(previous_enabled: bool, previous_sink: Callable) -> void:
	_previous_enabled = previous_enabled
	_previous_sink = previous_sink


## Restores the debug state captured when this scope opened.
func close() -> void:
	if _closed:
		return
	_closed = true
	Netw.dbg._close_scope(_previous_enabled, _previous_sink)
