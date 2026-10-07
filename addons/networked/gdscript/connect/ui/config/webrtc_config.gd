## WebRTC defaults for a [ConnectBrowser].
class_name ConnectWebRTCConfig
extends ConnectTransportConfig

@export var signaling_namespace: String = ""
@export var trackers: PackedStringArray = [
	"wss://tracker.openwebtorrent.com",
	"wss://tracker.webtorrent.dev",
]
@export var ice_servers: Array[ConnectIceServerConfig] = [
	ConnectIceServerConfig.new(),
]
@export_range(0.01, 60.0, 0.01) var connect_retry: float = 8.0
@export_range(1, 100) var max_connect_attempts: int = 3
@export_range(0.01, 60.0, 0.01) var gather_timeout: float = 6.0
@export_range(0, 4095) var max_players: int = 0


func peer_class() -> StringName:
	return &"WebRTCMultiplayerPeer"


func settings() -> Dictionary:
	var servers: Array[Dictionary] = []
	for server in ice_servers:
		if server != null:
			servers.append(server.settings())
	return {
		signaling_namespace = signaling_namespace,
		trackers = trackers.duplicate(),
		ice_servers = servers,
		connect_retry = connect_retry,
		max_connect_attempts = max_connect_attempts,
		gather_timeout = gather_timeout,
		max_players = max_players,
	}
