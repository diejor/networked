## Selects [LocalTransport], which needs no authored inputs.
##
## The in-process [LocalLoopbackSession] binds nothing and addresses nothing, so
## these params carry only their scheme. They exist so a session that runs on
## the loopback is authored the same typed way as one that runs on a socket.
## [codeblock]
## config.transport = NetwLocalParams.new()   # config.scheme now reads &"local"
## [/codeblock]
class_name NetwLocalParams
extends NetwTransportParams

func _scheme() -> StringName:
	return &"local"


func _to_dict() -> Dictionary:
	return { }


func _from_dict(_source: Dictionary) -> PackedStringArray:
	return PackedStringArray()
