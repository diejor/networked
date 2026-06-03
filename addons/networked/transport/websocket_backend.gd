## [BackendPeer] implementation using [WebSocketMultiplayerPeer].
##
## Supports local [code]ws://[/code] and production [code]wss://[/code]
## connections. Compatible with web exports.
## [codeblock]
## var target := JoinTarget.new()
## target.backend = WebSocketBackend.new()
## target.address = "ws://localhost:21253"
## [/codeblock]
@tool
class_name WebSocketBackend
extends BackendPeer

## TCP port the server listens on.
@export var port: int = 21253
## Hostname used for WSS connections when no explicit address is supplied.
@export var public_host: String
## Maximum number of simultaneous client connections allowed by the server.
@export var max_clients: int = 32


## Implements [method BackendPeer.create_host_peer] with
## [method WebSocketMultiplayerPeer.create_server].
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("WebSocketBackend: create_host_peer called.")
	var peer := WebSocketMultiplayerPeer.new()
	peer.set_outbound_buffer_size(1048576) # 1MB
	var err := peer.create_server(port)
	if err != OK:
		Netw.dbg.info(
			"WebSocket create_server failed: %s",
			[error_string(err)],
			func(m): push_warning(m)
		)
		return null
	Netw.dbg.info("WebSocket server ready on *:%d", [port])
	return peer


## Implements [method BackendPeer.create_join_peer] with
## [method WebSocketMultiplayerPeer.create_client].
func create_join_peer(
		_tree: MultiplayerTree,
		server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace(
		"WebSocketBackend: create_join_peer called at %s",
		[server_address],
	)
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


## Implements [method BackendPeer.query_server_info] with [AuthProbeClient].
##
## [method build_url] normalizes [param address] before the probe opens a
## temporary WebSocket connection.
func query_server_info(
		address: String,
		timeout: float = 2.0,
) -> ServerInfoResult:
	var probe := AuthProbeClient.new(self)
	return await probe.query(address, timeout)


## Returns a probed [code]"Server URL"[/code] [AddressHint].
func get_address_hint() -> AddressHint:
	var hint := AddressHint.make(
		"Server URL",
		"ws://localhost:%d" % port,
		"Empty -> wss://%s. Use localhost or ws[s]:// URLs." % public_host,
		true,
		true,
	)
	return hint


## Builds the WebSocket URL from [param server_address].
##
## Empty address maps to [code]wss://[member public_host][/code] when
## [member public_host] is configured, falling back to localhost otherwise.
## Localhost maps to [code]ws://localhost:[member port][/code]. If the address
## is already a full [code]ws://[/code] or [code]wss://[/code] URL, it is
## returned unchanged.
func build_url(server_address: String) -> String:
	if server_address.is_empty():
		if not public_host.is_empty():
			return "wss://" + public_host
		return "ws://localhost:" + str(port)

	if server_address == "localhost" or server_address == "127.0.0.1":
		return "ws://localhost:" + str(port)

	if server_address.begins_with("ws://") or \
			server_address.begins_with("wss://"):
		return server_address

	return "wss://" + server_address


## Implements [method BackendPeer.can_host]. Browsers cannot open a listening
## socket, so a web client can join but not host.
func can_host() -> bool:
	return not OS.has_feature("web")


## Returns the display name for this backend.
func get_display_name() -> String:
	return "WebSocket"
