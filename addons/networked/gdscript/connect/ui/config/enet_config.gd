## ENet defaults for a [ConnectBrowser].
class_name ConnectENetConfig
extends ConnectTransportConfig

@export_range(1, 65535) var port: int = 21253
@export_range(1, 4095) var max_players: int = 32


func peer_class() -> StringName:
	return &"ENetMultiplayerPeer"


func settings() -> Dictionary:
	return { port = port, max_players = max_players }
