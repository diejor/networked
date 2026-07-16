## A [NetwTransport] over [ENetMultiplayerPeer] for LAN and direct IP.
##
## Recognizes the [code]&"enet"[/code] scheme and reads the port from
## [member NetwHostConfig.params] or the [code]host:port[/code] form of
## [member NetwConnectTarget.address]. ENet has no web export, so
## [method _is_available] is false there.
class_name ENetTransport
extends NetwTransport

const DEFAULT_PORT := 21253
const DEFAULT_MAX_CLIENTS := 32


func scheme() -> StringName:
	return &"enet"


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"enet"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.scheme == &"enet"


func _can_view(peer: MultiplayerPeer) -> bool:
	return peer is ENetMultiplayerPeer


func _host(
		_attempt: NetwConnectAttempt,
		config: NetwHostConfig,
) -> MultiplayerPeer:
	var port := int(config.params.get("port", DEFAULT_PORT))
	var max_clients := int(config.params.get("max_clients", DEFAULT_MAX_CLIENTS))
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, max_clients)
	if err != OK:
		Netw.dbg.warn(
			"ENet create_server failed: %s",
			[error_string(err)],
			func(m): push_warning(m),
		)
		return null
	return peer


func _join(
		_attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	return _make_client(target)


func _make_probe_peer(target: NetwConnectTarget) -> MultiplayerPeer:
	return _make_client(target)


func _address_hint() -> NetwAddressHint:
	return NetwAddressHint.make(
		"Server IP",
		"localhost",
		"Empty or 'localhost' connects locally. Use host:port or an IPv4/IPv6 "
		+ "address for remote.",
		true,
		true,
	)


func _is_available() -> bool:
	return not OS.has_feature("web")


func _display_name() -> String:
	return "ENet"


func _capabilities() -> int:
	return Capability.SUPPORTS_PROBE | Capability.LISTEN_FALLBACK


# Builds a client peer from the target's address or metadata. The address
# carries the canonical host:port form; a structured metadata port or host
# fills in whichever the address omits.
func _make_client(target: NetwConnectTarget) -> MultiplayerPeer:
	var address := target.address
	var port := int(target.metadata.get("port", DEFAULT_PORT))
	var host := "localhost"
	if not address.is_empty():
		var sep := address.rfind(":")
		if sep > 0:
			host = address.substr(0, sep)
			port = address.substr(sep + 1).to_int()
		else:
			host = address
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(host, port)
	if err != OK:
		Netw.dbg.error("ENet create_client failed: %s", [error_string(err)])
		return null
	return peer
