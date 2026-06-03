## [BackendPeer] implementation using [ENetMultiplayerPeer].
##
## Suitable for LAN and direct IP connections. Not available in web exports.
## [codeblock]
## var target := JoinTarget.new()
## target.backend = ENetBackend.new()
## target.address = "127.0.0.1"
## [/codeblock]
@tool
class_name ENetBackend
extends BackendPeer

## UDP port used by [method create_host_peer] and [method create_join_peer].
@export var port: int = 21253
## Maximum number of simultaneous client connections allowed by the server.
@export var max_clients: int = 32


## Implements [method BackendPeer.create_host_peer] with
## [method ENetMultiplayerPeer.create_server].
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("ENetBackend: create_host_peer called.")
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_clients)
	if err != OK:
		Netw.dbg.warn(
			"ENet create_server failed: %s",
			[error_string(err)],
			func(m): push_warning(m)
		)
		return null
	Netw.dbg.info("ENet server ready on port %d", [port])
	return peer


## Implements [method BackendPeer.create_join_peer] with
## [method ENetMultiplayerPeer.create_client].
func create_join_peer(
		_tree: MultiplayerTree,
		server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"ENetBackend: create_join_peer called at %s",
		[server_address],
	)
	var peer := ENetMultiplayerPeer.new()
	if server_address.is_empty():
		server_address = "localhost"

	var err := peer.create_client(server_address, port)
	if err != OK:
		Netw.dbg.error("ENet create_client failed: %s", [error_string(err)])
		return null
	Netw.dbg.info("ENet client connecting to %s:%d", [server_address, port])
	return peer


## Implements [method BackendPeer.query_server_info] with [AuthProbeClient].
##
## ENet can probe the same host and port that [method create_join_peer] uses.
func query_server_info(
		address: String,
		timeout: float = 2.0,
) -> ServerInfoResult:
	var probe := AuthProbeClient.new(self)
	return await probe.query(address, timeout)


## Returns a probed [code]"Server IP"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Server IP",
		"localhost",
		"Empty or 'localhost' connects to a local host. Use host:port or an "
		+ "IPv4/IPv6 address for remote.",
		true,
		true,
	)


## Implements [method BackendPeer.is_available]. ENet has no web export.
func is_available() -> bool:
	return not OS.has_feature("web")


## Returns the display name for this backend.
func get_display_name() -> String:
	return "ENet"
