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

## Initializes the loopback server peer. Returns [code]OK[/code].
func host() -> Error:
	if not session.has_live_server():
		session.reset()
	api.multiplayer_peer = session.get_server_peer()
	NetLog.info("WebRTC loopback server ready.")
	return OK

## Creates a loopback client peer and links it to the server. Returns [code]OK[/code].
func join(_server_address: String, _username: String = "") -> Error:
	api.multiplayer_peer = session.get_client_peer()
	NetLog.info("WebRTC loopback client ready.")
	return OK

func poll(dt: float) -> void:
	if session:
		session.poll()
	
	super.poll(dt)
