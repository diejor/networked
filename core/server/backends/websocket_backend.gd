extends MultiplayerServerBackend
class_name WebSocketServerBackend

@export var port: int = 21253

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return multiplayer_peer as WebSocketMultiplayerPeer

func _init() -> void:
	multiplayer_peer = WebSocketMultiplayerPeer.new()

func create_server() -> Error:
	var err := ws_peer.create_server(port)
	match err:
		OK:
			print("WebSocket server ready on *:%d" % port)
			return OK
		_:
			return err


func peer_reset_state() -> void:
	if (multiplayer_api.has_multiplayer_peer() 
		and multiplayer_api.multiplayer_peer is WebSocketMultiplayerPeer):
		if ws_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED:
			ws_peer.close()

	multiplayer_api.multiplayer_peer = null
	multiplayer_peer = WebSocketMultiplayerPeer.new()
