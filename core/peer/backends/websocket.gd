class_name WebSocketBackend
extends BackendPeer

@export var port: int = 21253
@export var public_host: String = "ws.diejor.tech"

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return api.multiplayer_peer as WebSocketMultiplayerPeer
	set(peer):
		api.multiplayer_peer = peer

func host() -> Error:
	var peer := WebSocketMultiplayerPeer.new()
	var err := peer.create_server(port)
	
	if err == OK:
		ws_peer = peer
		print("WebSocket server ready on *:%d" % port)
		return OK
		
	return err

func join(server_address: String, _username: String = "") -> Error:
	var peer := WebSocketMultiplayerPeer.new()
	var url := build_url(server_address)

	var err := peer.create_client(url)
	if err != OK:
		push_warning("Can't create client (%s) to %s" % [error_string(err), url])
		return err
	
	ws_peer = peer
	print("Client connecting to ", url)
	return OK

func build_url(server_address: String) -> String:
	if server_address.is_empty():
		return "wss://" + public_host

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	return "wss://" + server_address
