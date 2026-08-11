## The typed inputs [ENetTransport] opens a listening socket with.
##
## ENet needs a port before it can bind, so these are host-side inputs. A
## joining client reads its port from the [code]host:port[/code] form of
## [member NetwConnectTarget.address] instead, which is why nothing here is
## required to join.
## [codeblock]
## var enet := NetwENetParams.new()
## enet.port = 21253
## enet.max_clients = 8
## config.transport = enet          # config.scheme now reads &"enet"
## [/codeblock]
class_name NetwENetParams
extends NetwTransportParams

## Port the listening socket binds.
@export var port: int = 21253

## Peers the listening socket admits.
@export_range(1, 4095) var max_clients: int = 32


func _scheme() -> StringName:
	return &"enet"


func _to_dict() -> Dictionary:
	return { "port": port, "max_clients": max_clients }


func _from_dict(source: Dictionary) -> PackedStringArray:
	port = int(source.get("port", port))
	max_clients = int(source.get("max_clients", max_clients))
	return PackedStringArray(["port", "max_clients"])
