## [BackendPeer] implementation using [WebSocketMultiplayerPeer].
##
## Supports both [code]ws://[/code] (local) and [code]wss://[/code] (production) connections
## and is compatible with web exports.
@tool
class_name WebSocketBackend
extends BackendPeer

## TCP port the server listens on.
@export var port: int = 21253
## Hostname used for WSS connections when no explicit address is supplied.
@export var public_host: String = "ws.diejor.tech"

var ws_peer: WebSocketMultiplayerPeer:
	get:
		return api.multiplayer_peer as WebSocketMultiplayerPeer
	set(peer):
		api.multiplayer_peer = peer

func host() -> Error:
	NetLog.trace("WebSocketBackend: host called.")
	var peer := WebSocketMultiplayerPeer.new()
	var err := peer.create_server(port)
	
	if err == OK:
		ws_peer = peer
		NetLog.info("WebSocket server ready on *:%d" % port)
		return OK
	
	return err

func join(server_address: String, _username: String = "") -> Error:
	NetLog.trace("WebSocketBackend: join called at %s" % server_address)
	var peer := WebSocketMultiplayerPeer.new()
	var url := build_url(server_address)
	NetLog.debug("WebSocket connecting to URL: %s" % url)

	var err := peer.create_client(url)
	if err == OK:
		ws_peer = peer
		NetLog.info("Client connecting to %s" % url)
		return OK
	
	return err

## Builds the WebSocket URL from [param server_address].
##
## Empty address maps to [code]wss://[member public_host][/code]; localhost maps to
## [code]ws://localhost:[member port][/code]; anything else maps to [code]wss://[param server_address][/code].
func build_url(server_address: String) -> String:
	if server_address.is_empty():
		return "wss://" + public_host

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	return "wss://" + server_address

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
