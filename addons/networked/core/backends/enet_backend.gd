@tool
class_name ENetBackend
extends BackendPeer

@export var port: int = 21253
@export var max_clients: int = 32


func host() -> Error:
	NetLog.trace("ENetBackend: host called.")
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_clients)
	
	if err == OK:
		api.multiplayer_peer = peer
		NetLog.info("ENet server ready on port %d" % port)
	else:
		NetLog.error("Failed to create ENet server: %s." % error_string(err))
	
	return err

func join(server_address: String, _username: String = "") -> Error:
	NetLog.trace("ENetBackend: join called at %s" % server_address)
	var peer := ENetMultiplayerPeer.new()
	
	# Default to localhost if no address is provided
	if server_address.is_empty():
		server_address = "localhost"
		
	var err := peer.create_client(server_address, port)
	
	if err == OK:
		api.multiplayer_peer = peer
		NetLog.info("ENet client connecting to %s:%d" % [server_address, port])
	else:
		NetLog.error("Failed to create ENet client: %s" % error_string(err))
		
	return err


func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
