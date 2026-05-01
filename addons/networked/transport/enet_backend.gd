## [BackendPeer] implementation using Godot's built-in [ENetMultiplayerPeer].
##
## Suitable for LAN and direct IP connections. Not available in web exports.
@tool
class_name ENetBackend
extends BackendPeer

## UDP port the server listens on and clients connect to.
@export var port: int = 21253
## Maximum number of simultaneous client connections allowed by the server.
@export var max_clients: int = 32

func host() -> Error:
	Netw.dbg.trace("ENetBackend: host called.")
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_clients)
	
	if err == OK:
		api.multiplayer_peer = peer
		Netw.dbg.info("ENet server ready on port %d", [port])
	
	return err

func join(server_address: String, _username: String = "") -> Error:
	Netw.dbg.trace("ENetBackend: join called at %s", [server_address])
	var peer := ENetMultiplayerPeer.new()
	if server_address.is_empty():
		server_address = "localhost"
	
	var err := peer.create_client(server_address, port)
	
	if err == OK:
		api.multiplayer_peer = peer
		Netw.dbg.info("ENet client connecting to %s:%d", [server_address, port])
	
	return err

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
