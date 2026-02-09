extends MultiplayerServerBackend
class_name WebSocketServerBackend

@export var port: int = 21253

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return api.multiplayer_peer as WebSocketMultiplayerPeer
	set(peer):
		api.multiplayer_peer = peer

func create_server() -> Error:
	var peer := WebSocketMultiplayerPeer.new()
	var err := peer.create_server(port)
	match err:
		OK:
			ws_peer = peer
			print("WebSocket server ready on *:%d" % port)
			return OK
		_:
			return err
