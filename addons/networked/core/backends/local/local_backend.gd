## [BackendPeer] that routes packets through an in-process [LocalLoopbackSession].
##
## Used automatically by [NetworkSession] when running on the web with a non-WebRTC backend,
## ensuring a fast, allocation-free loopback without any real network sockets.
@tool
class_name LocalLoopbackBackend
extends BackendPeer

## The shared in-process loopback session.
var session: LocalLoopbackSession = LocalLoopbackSession.get_shared_session()

## Initializes the loopback server peer. Returns [code]OK[/code].
func host() -> Error:
	NetLog.trace("LocalLoopbackBackend: host called.")
	if not session.has_live_server():
		session.reset()
	api.multiplayer_peer = session.get_server_peer()
	NetLog.info("Local loopback server ready.")
	return OK

## Creates a new loopback client peer and links it to the server. Returns [code]OK[/code].
func join(_server_address: String, _username: String = "") -> Error:
	NetLog.trace("LocalLoopbackBackend: join called.")
	api.multiplayer_peer = session.create_client_peer()
	NetLog.info("Local loopback client ready.")
	return OK

func poll(dt: float) -> void:
	if session:
		session.poll()
	super.poll(dt)


func _copy_from(source: BackendPeer) -> void:
	session = (source as LocalLoopbackBackend).session


func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
