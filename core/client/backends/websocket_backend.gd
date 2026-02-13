class_name WebSocketClientBackend
extends MultiplayerClientBackend


@export var port: int = 21253
@export var public_host: String = "ws.diejor.tech"

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return api.multiplayer_peer as WebSocketMultiplayerPeer
	set(peer):
		api.multiplayer_peer = peer

func build_url(server_address: String) -> String:
	if server_address.is_empty():
		return "wss://" + public_host

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	return "wss://" + server_address

func create_connection(server_address: String, _username: String) -> Error:
	var peer := WebSocketMultiplayerPeer.new()
	var url := build_url(server_address)

	var err := peer.create_client(url)
	if err != OK:
		push_warning("Can't create client (%s) to %s" % [error_string(err), url])
		return err
	
	ws_peer = peer
	print("Client connecting to ", url)
	return OK
