## Selects [NakamaTransport], whose inputs come from the match rather than the
## caller.
##
## A Nakama session is addressed by match id, and the server name, visibility,
## and player cap are already fields of [NetwHostConfig], so nothing is left for
## the transport to be told. These params carry only their scheme.
## [codeblock]
## config.transport = NetwNakamaParams.new()  # config.scheme now reads &"nakama"
## config.max_players = 8                     # the match reads these
## [/codeblock]
class_name NetwNakamaParams
extends NetwTransportParams

func _scheme() -> StringName:
	return &"nakama"


func _to_dict() -> Dictionary:
	return { }


func _from_dict(_source: Dictionary) -> PackedStringArray:
	return PackedStringArray()
