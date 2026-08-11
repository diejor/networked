## Selects [SteamTransport], whose inputs come from the lobby rather than the
## caller.
##
## A Steam session is addressed by lobby id, and the server name, visibility,
## and player cap are already fields of [NetwHostConfig], so nothing is left for
## the transport to be told. These params carry only their scheme.
## [codeblock]
## config.transport = NetwSteamParams.new()   # config.scheme now reads &"steam"
## config.server_name = "My Lobby"            # the lobby reads these
## [/codeblock]
class_name NetwSteamParams
extends NetwTransportParams

func _scheme() -> StringName:
	return &"steam"


func _to_dict() -> Dictionary:
	return { }


func _from_dict(_source: Dictionary) -> PackedStringArray:
	return PackedStringArray()
