## The typed inputs [WebSocketTransport] opens a listening socket with.
##
## A web build cannot open a listening socket at all, so these are host-side
## inputs a native build authors. A joining client reads the full URL from
## [member NetwConnectTarget.address].
## [codeblock]
## var ws := NetwWebSocketParams.new()
## ws.port = 21253
## config.transport = ws            # config.scheme now reads &"ws"
## [/codeblock]
class_name NetwWebSocketParams
extends NetwTransportParams

## Port the listening socket binds.
@export var port: int = 21253


func _scheme() -> StringName:
	return &"ws"


func _to_dict() -> Dictionary:
	return { "port": port }


func _from_dict(source: Dictionary) -> PackedStringArray:
	port = int(source.get("port", port))
	return PackedStringArray(["port"])
