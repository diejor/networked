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


## Direct ENet hosts answer the same-port [code]NPRB[/code] probe during the
## [SceneMultiplayer] auth phase. See [AuthProbeClient].
func query_server_info(
	address: String, timeout: float = 2.0,
) -> ServerInfoResult:
	var probe := AuthProbeClient.new(self)
	return await probe.query(address, timeout)


func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Server IP",
		"localhost",
		"Empty or 'localhost' connects to a local host. Use host:port or an "
		+ "IPv4/IPv6 address for remote.",
		true,
		true
	)


func get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []


## Returns the user-facing friendly name for this backend.
func get_display_name() -> String:
	return "ENet"
