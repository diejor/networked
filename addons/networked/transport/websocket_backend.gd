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


func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("WebSocketBackend: create_host_peer called.")
	var peer := WebSocketMultiplayerPeer.new()
	peer.set_outbound_buffer_size(1048576) # 1MB
	var err := peer.create_server(port)
	if err != OK:
		Netw.dbg.warn("WebSocket create_server failed: %s", [error_string(err)],
		func(m): push_warning(m))
		return null
	Netw.dbg.info("WebSocket server ready on *:%d", [port])
	return peer


func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace("WebSocketBackend: create_join_peer called at %s", [server_address])
	var peer := WebSocketMultiplayerPeer.new()
	peer.set_outbound_buffer_size(1048576) # 1MB
	var url := build_url(server_address)
	Netw.dbg.debug("WebSocket connecting to URL: %s", [url])

	var err := peer.create_client(url)
	if err != OK:
		Netw.dbg.error("WebSocket create_client failed: %s", [error_string(err)])
		return null
	Netw.dbg.info("Client connecting to %s", [url])
	return peer


## Builds the WebSocket URL from [param server_address].
##
## Empty address maps to [code]wss://[member public_host][/code]; localhost
## maps to [code]ws://localhost:[member port][/code]; anything else maps to
## [code]wss://[param server_address][/code].
func build_url(server_address: String) -> String:
	if server_address.is_empty():
		return "wss://" + public_host

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	return "wss://" + server_address

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
