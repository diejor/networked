## WebRTC defaults shared by browser forms and room discovery.
class_name ConnectWebRTCConfig
extends ConnectTransportConfig

@export_group("Shared")
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
@export_group("Host")
@export var room_name: String = ""
@export_range(0, 4095) var max_players: int = 0


func peer_class() -> StringName:
	return &"WebRTCMultiplayerPeer"


func browse_settings() -> Dictionary:
	return {
		trackers = trackers.duplicate(),
		signaling_namespace = signaling_namespace,
	}


func client_settings() -> Dictionary:
	var servers: Array[Dictionary] = []
	for server: ConnectIceServerConfig in ice_servers:
		if server != null:
			servers.append(server.settings())
	var result := browse_settings()
	result.ice_servers = servers
	result.connect_retry = connect_retry
	result.max_connect_attempts = max_connect_attempts
	result.gather_timeout = gather_timeout
	return result


func host_settings() -> Dictionary:
	var result := client_settings()
	result.room_name = room_name
	result.max_players = max_players
	return result
