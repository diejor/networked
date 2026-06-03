## [BackendPeer] that routes packets through [WebRTCLoopbackSession].
##
## Uses the WebRTC offer, answer, and ICE handshake entirely in memory. This
## keeps web exports on the WebRTC code path without external signaling.
@tool
class_name WebRTCLoopbackBackend
extends BackendPeer

## Shared WebRTC loopback session.
var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")


## Implements [method BackendPeer.create_host_peer] with [member session].
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	if not session.has_live_server():
		session.reset()
	elif session.pc_server \
			and session.pc_server.get_connection_state() \
					!= WebRTCPeerConnection.STATE_NEW:
		session.reset()
	Netw.dbg.info("WebRTC loopback server ready.")
	return session.get_server_peer()


## Implements [method BackendPeer.create_join_peer] with [member session].
func create_join_peer(
		_tree: MultiplayerTree,
		_server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	if not session.has_live_server():
		Netw.dbg.warn(
			"WebRTC loopback: no live server to join.",
			func(m): push_warning()
		)
		return null
	Netw.dbg.info("WebRTC loopback client ready.")
	return session.get_client_peer()


## Implements [method BackendPeer.poll] by polling [member session].
func poll(_dt: float) -> void:
	if session:
		session.poll()
