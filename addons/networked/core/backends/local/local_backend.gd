@tool
class_name LocalLoopbackBackend
extends BackendPeer

var session: LocalLoopbackSession = LocalLoopbackSession.get_shared_session()


func host() -> Error:
	NetLog.trace("LocalLoopbackBackend: host called.")
	if not session.has_live_server():
		session.reset()
	api.multiplayer_peer = session.get_server_peer()
	NetLog.info("Local loopback server ready.")
	return OK

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
