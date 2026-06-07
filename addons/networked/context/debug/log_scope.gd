## Restores a [NetwLog] override when [method close] is called.
##
## Keep the returned scope in a variable for as long as the override should
## remain active.
## [codeblock]
## var scope := NetwLog.scoped("trace")
## # ...
## scope.close()
## [/codeblock]
class_name NetwLogScope
extends RefCounted

var _settings: NetwLogSettings
var _closed := false


func _init(settings: NetwLogSettings) -> void:
	_settings = settings


## Restores the log settings that were active before this scope opened.
func close() -> void:
	if _closed:
		return
	_closed = true
	NetwLog._close_scope(_settings)
	_settings = null
