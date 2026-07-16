## A [NetwTransport] routing WebRTC entirely through the in-memory
## [WebRTCLoopbackSession].
##
## Runs the offer, answer, and ICE handshake in process with no signaling, which
## keeps web exports on the WebRTC code path without a tracker. Like
## [LocalTransport] the session is process-global, so the transport stays
## stateless and the per-frame pump lives on the view.
class_name WebRTCLoopbackTransport
extends NetwTransport

var _session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")


func _can_join(target: NetwConnectTarget) -> bool:
	return target != null and target.scheme == &"webrtc"


func _can_host(config: NetwHostConfig) -> bool:
	return config != null and config.scheme == &"webrtc"


func _host(
		_attempt: NetwConnectAttempt,
		_config: NetwHostConfig,
) -> MultiplayerPeer:
	if not _session.has_live_server():
		_session.reset()
	elif _session.pc_server \
			and _session.pc_server.get_connection_state() \
					!= WebRTCPeerConnection.STATE_NEW:
		_session.reset()
	return _session.get_server_peer()


func _join(
		_attempt: NetwConnectAttempt,
		_target: NetwConnectTarget,
) -> MultiplayerPeer:
	if not _session.has_live_server():
		Netw.dbg.warn(
			"WebRTC loopback: no live server to join.",
			func(m): push_warning(m),
		)
		return null
	return _session.get_client_peer()


func _make_view(
		peer: MultiplayerPeer,
		_attempt: NetwConnectAttempt = null,
) -> NetwPeerView:
	return LoopbackView.new(peer, _session)


func _display_name() -> String:
	return "WebRTC (loopback)"


# Pumps the shared loopback session so its in-memory handshake advances.
class LoopbackView:
	extends NetwPeerView

	var _session: WebRTCLoopbackSession

	func _init(peer: MultiplayerPeer = null, session: WebRTCLoopbackSession = null) -> void:
		super(peer)
		_session = session

	func display_name() -> String:
		return "WebRTC (loopback)"

	func poll(_dt: float) -> void:
		if _session:
			_session.poll()

	func close() -> void:
		_session = null
		super()
