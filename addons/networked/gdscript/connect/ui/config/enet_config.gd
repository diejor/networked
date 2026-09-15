## ENet defaults authored on [ConnectBrowser].
class_name ConnectENetConfig
extends ConnectTransportConfig

@export_range(1, 65535) var port: int = 21253
@export_range(1, 4095) var max_players: int = 32


func peer_class() -> StringName:
	return &"ENetMultiplayerPeer"


func client_settings() -> Dictionary:
	return { port = port }


func host_settings() -> Dictionary:
	return { port = port, max_players = max_players }
