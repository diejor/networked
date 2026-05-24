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

func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("ENetBackend: create_host_peer called.")
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_clients)
	if err != OK:
		Netw.dbg.warn("ENet create_server failed: %s", [error_string(err)],
		func(m): push_warning(m))
		return null
	Netw.dbg.info("ENet server ready on port %d", [port])
	return peer

func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace("ENetBackend: create_join_peer called at %s", [server_address])
	var peer := ENetMultiplayerPeer.new()
	if server_address.is_empty():
		server_address = "localhost"

	var err := peer.create_client(server_address, port)
	if err != OK:
		Netw.dbg.error("ENet create_client failed: %s", [error_string(err)])
		return null
	Netw.dbg.info("ENet client connecting to %s:%d", [server_address, port])
	return peer


## Synchronous UDP bind-test on the configured port. ENet servers hold the
## UDP port exclusively, so a failed bind means a server is presumed live.
func probe(address: String, _timeout: float = 0.2) -> ProbeResult:
	if not _is_local_address(address):
		return ProbeResult.unsupported()

	var probe_socket := PacketPeerUDP.new()
	var err := probe_socket.bind(port)
	if err == ERR_ALREADY_IN_USE:
		return ProbeResult.reachable(0, { "via": "bind-test" })
	if err != OK:
		return ProbeResult.error(error_string(err))
	probe_socket.close()
	return ProbeResult.unreachable({ "via": "bind-test" })


func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Server IP",
		"localhost",
		"Empty or 'localhost' connects to a local host. Use host:port or an "
		+ "IPv4/IPv6 address for remote.",
		true,
		true
	)


func _is_local_address(address: String) -> bool:
	return (address.is_empty()
		or address == "localhost"
		or address == "127.0.0.1")


func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
