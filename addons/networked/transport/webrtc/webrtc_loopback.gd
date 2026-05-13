## [BackendPeer] that routes packets through an in-process [WebRTCLoopbackSession].
##
## Uses the full WebRTC offer/answer/ICE handshake entirely in memory, so it works
## in web exports without real WebRTC sockets.
@tool
class_name WebRTCLoopbackBackend
extends BackendPeer

## The shared in-process WebRTC loopback session.
var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []

func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	if not session.has_live_server():
		session.reset()
	elif session.pc_server \
			and session.pc_server.get_connection_state() \
			!= WebRTCPeerConnection.STATE_NEW:
		session.reset()
	Netw.dbg.info("WebRTC loopback server ready.")
	return session.get_server_peer()

func create_join_peer(
	_tree: MultiplayerTree, _server_address: String, _username: String = ""
) -> MultiplayerPeer:
	if not session.has_live_server():
		Netw.dbg.warn("WebRTC loopback: no live server to join.",
		func(m): push_warning())
		return null
	Netw.dbg.info("WebRTC loopback client ready.")
	return session.get_client_peer()

func poll(_dt: float) -> void:
	if session:
		session.poll()
