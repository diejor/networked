## A [NetwTransport] over Steam's P2P matchmaking.
##
## Recognizes the [code]&"steam"[/code] scheme and delegates every lobby
## operation to the [SteamLobbyDirectory] service resolved from the session, so
## the transport itself holds no Steam state. A [member NetwConnectTarget.address]
## is the lobby id. With no directory registered the attempt resolves a clean
## error rather than crashing.
class_name SteamTransport
extends NetwTransport

func _scheme() -> StringName:
	return &"steam"


func _params_from_dict(source: Dictionary) -> NetwTransportParams:
	var params := NetwSteamParams.new()
	params.from_dict(source)
	return params


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"steam"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.transport is NetwSteamParams


func _host(
		attempt: NetwConnectAttempt,
		config: NetwHostConfig,
) -> MultiplayerPeer:
	var dir := _resolve_dir(attempt)
	if dir == null:
		attempt.resolve(
			NetwConnectResult.error(
				"Steam lobby directory is not registered.",
			),
		)
		return null
	_route_p2p_failure(dir, attempt)
	return await dir._host_lobby(config)


func _join(
		attempt: NetwConnectAttempt,
		target: NetwConnectTarget,
) -> MultiplayerPeer:
	var dir := _resolve_dir(attempt)
	if dir == null:
		attempt.resolve(
			NetwConnectResult.error(
				"Steam lobby directory is not registered.",
			),
		)
		return null
	_route_p2p_failure(dir, attempt)
	return await dir._join_lobby_peer(int(target.address))


func _is_available() -> bool:
	return not OS.has_feature("web")


func _display_name() -> String:
	return "Steam"


func _address_hint() -> NetwAddressHint:
	return NetwAddressHint.make(
		"Lobby ID",
		"",
		"Steam lobby IDs are discovered through the server browser.",
		false,
		false,
	)


# Resolves the Steam lobby directory service for this session.
func _resolve_dir(attempt: NetwConnectAttempt) -> SteamLobbyDirectory:
	var api := attempt.api
	if api == null:
		return null
	var dir := api.get_service(SteamLobbyDirectory) as SteamLobbyDirectory
	if dir == null:
		dir = api.get_service(LobbyDirectory) as SteamLobbyDirectory
	return dir


# Builds directory host options from the typed host config.


# Resolves the attempt on a Steam P2P failure while it is establishing. The
# is_done() guard keeps a late failure from touching a completed attempt, and the
# binding is dropped once the attempt resolves.
func _route_p2p_failure(
		dir: SteamLobbyDirectory,
		attempt: NetwConnectAttempt,
) -> void:
	var handler := func(_reason: String) -> void:
		if not attempt.is_done():
			attempt.resolve(
				NetwConnectResult.unreachable(
					&"STEAM_P2P_FAILED",
					"Steam peer connection failed.",
				),
			)
	dir.peer_connect_failed.connect(handler)
	attempt.finished.connect(
		func(_r: NetwConnectResult) -> void:
			if dir.peer_connect_failed.is_connected(handler):
				dir.peer_connect_failed.disconnect(handler),
		CONNECT_ONE_SHOT,
	)
