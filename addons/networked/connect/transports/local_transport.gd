## A [NetwTransport] over the in-process [LocalLoopbackSession].
##
## Recognizes the [code]&"local"[/code] scheme and routes packets through the
## process-global [LocalLoopbackSession] with no real sockets, which is what web
## exports use for a non-WebRTC session. The transport stays stateless: the
## shared session is process-global and its per-frame pump lives on
## [LocalPeerView].
class_name LocalTransport
extends NetwTransport

func _scheme() -> StringName:
	return &"local"


func _params_from_dict(source: Dictionary) -> NetwTransportParams:
	var params := NetwLocalParams.new()
	params.from_dict(source)
	return params


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"local"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.transport is NetwLocalParams


func _host(
		attempt: NetwConnectAttempt,
		_config: NetwHostConfig,
) -> MultiplayerPeer:
	var session := LocalLoopbackSession.get_shared_session()
	if not session.has_live_server():
		session.reset()
	var a := attempt.api
	session.server_app_id = a.session.app_id if a else &""
	return session.get_server_peer()


func _join(
		_attempt: NetwConnectAttempt,
		_target: NetwConnectTarget,
) -> MultiplayerPeer:
	var session := LocalLoopbackSession.get_shared_session()
	if not session.has_live_server():
		Netw.dbg.warn(
			"Local loopback: no live server to join.",
			func(m): push_warning(m),
		)
		return null
	return session.create_client_peer()


# A probe only reads an already-live in-process server. Returning null when no
# server is up reports the target unreachable, so join_or_host hosts instead of
# double-binding the shared server peer to a second SceneMultiplayer.
func _make_probe_peer(_target: NetwConnectTarget) -> MultiplayerPeer:
	var session := LocalLoopbackSession.get_shared_session()
	if not session.has_live_server():
		return null
	return session.create_client_peer()


func _make_view(
		peer: MultiplayerPeer,
		_attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	return LocalPeerView.new(peer, LocalLoopbackSession.get_shared_session())


func _address_hint() -> NetwAddressHint:
	var hint := NetwAddressHint.make(
		"",
		"",
		"In-process loopback. No address required.",
		true,
		false,
	)
	hint.hides_address_field = true
	return hint


func _display_name() -> String:
	return "Local"


func _capabilities() -> int:
	return Capability.LISTEN_FALLBACK
