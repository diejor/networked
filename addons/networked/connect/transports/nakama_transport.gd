## A [NetwTransport] over a Nakama relay match.
##
## Recognizes the [code]&"nakama"[/code] scheme and delegates hosting and joining
## to the [NakamaLobbyDirectory] service, which owns the authenticated socket and
## the relay [MultiplayerPeer]. A [member NetwConnectTarget.address] is the opaque
## match id. The relay needs no listening socket, so this transport can host on
## the web, unlike [WebSocketTransport]. With no directory registered the attempt
## resolves a clean error.
class_name NakamaTransport
extends NetwTransport

func _scheme() -> StringName:
	return &"nakama"


func _params_from_dict(source: Dictionary) -> NetwTransportParams:
	var params := NetwNakamaParams.new()
	params.from_dict(source)
	return params


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"nakama"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.transport is NetwNakamaParams


func _host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
) -> MultiplayerPeer:
	var dir := _resolve_dir(attempt)
	if dir == null:
		attempt.resolve(
			NetwConnectResult.error(
				"Nakama lobby directory is not registered.",
			),
		)
		return null
	return await dir._host_lobby(config)


func _join(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	var dir := _resolve_dir(attempt)
	if dir == null:
		attempt.resolve(
			NetwConnectResult.error(
				"Nakama lobby directory is not registered.",
			),
		)
		return null
	return await dir.join_match_peer(target.address)


func _make_view(
		peer: MultiplayerPeer,
		attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	if attempt == null:
		return null
	var dir := _resolve_dir(attempt)
	if dir == null:
		return null
	return NakamaPeerView.new(peer, dir)


func _is_available() -> bool:
	return NakamaWrapper.is_addon_present()


func _can_host_here() -> bool:
	return NakamaWrapper.is_addon_present()


func _display_name() -> String:
	return "Nakama"


func _address_hint() -> NetwAddressHint:
	return NetwAddressHint.make(
		"Match ID",
		"",
		"Paste the Nakama match id shared by the host.",
		false,
		false,
	)


# Resolves the Nakama lobby directory service for this session.
func _resolve_dir(attempt: NetwConnectAttempt) -> NakamaLobbyDirectory:
	var api := attempt.api
	if api == null:
		return null
	var dir := api.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	if dir == null:
		dir = api.get_service(LobbyDirectory) as NakamaLobbyDirectory
	return dir


# Builds directory host options from the typed host config.
