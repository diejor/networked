## A [NetwTransport] over [WebSocketMultiplayerPeer] for [code]ws://[/code] and
## [code]wss://[/code].
##
## Recognizes the [code]&"ws"[/code] scheme. A web client can join but cannot
## open a listening socket, so [method _can_host_here] is false on the web.
class_name WebSocketTransport
extends NetwTransport

const DEFAULT_PORT := 21253
const OUTBOUND_BUFFER := 1048576


func scheme() -> StringName:
	return &"ws"


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"ws"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.scheme == &"ws"


func _can_view(peer: MultiplayerPeer) -> bool:
	return peer is WebSocketMultiplayerPeer


func _make_view(
		peer: MultiplayerPeer,
		attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	if not (peer is WebSocketMultiplayerPeer):
		return null
	var port := int(attempt.context.get("port", 0)) if attempt else 0
	return WebSocketPeerView.new(peer, port)


func _host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
) -> MultiplayerPeer:
	var port := int(config.params.get("port", DEFAULT_PORT))
	var peer := WebSocketMultiplayerPeer.new()
	peer.set_outbound_buffer_size(OUTBOUND_BUFFER)
	var err := peer.create_server(port)
	if err != OK:
		Netw.dbg.warn(
			"WebSocket create_server failed: %s",
			[error_string(err)],
			func(m): push_warning(m),
		)
		return null
	# The peer never exposes its bound port, so the view reads it from the
	# attempt's build context.
	if attempt:
		attempt.context["port"] = port
	return peer


func _join(
		_attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	var peer := WebSocketMultiplayerPeer.new()
	peer.set_outbound_buffer_size(OUTBOUND_BUFFER)
	var err := peer.create_client(build_url(target.address))
	if err != OK:
		Netw.dbg.error("WebSocket create_client failed: %s", [error_string(err)])
		return null
	return peer


func _address_hint() -> NetwAddressHint:
	return NetwAddressHint.make(
		"Server URL",
		"ws://localhost:%d" % DEFAULT_PORT,
		"Empty connects to localhost. Use ws:// or wss:// URLs.",
		true,
		true,
	)


func _can_host_here() -> bool:
	return not OS.has_feature("web")


func _display_name() -> String:
	return "WebSocket"


func _capabilities() -> int:
	return Capability.SUPPORTS_PROBE | Capability.LISTEN_FALLBACK


## Normalizes [param address] into a [code]ws[s]://[/code] URL.
##
## An empty or localhost address maps to [code]ws://localhost[/code] on the
## default port. A bare host maps to [code]wss://[/code]. A full URL is returned
## unchanged.
func build_url(address: String) -> String:
	if address.is_empty() or address == "localhost" or address == "127.0.0.1":
		return "ws://localhost:" + str(DEFAULT_PORT)
	if address.begins_with("ws://") or address.begins_with("wss://"):
		return address
	return "wss://" + address
