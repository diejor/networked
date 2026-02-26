class_name LocalLoopbackBackend
extends BackendPeer

var session: LocalLoopbackSession = LocalLoopbackSession.get_shared_session()

func host() -> Error:
	if not session.has_live_server():
		session.reset()
	api.multiplayer_peer = session.get_server_peer()
	print("Local loopback server ready.")
	return OK

func join(_server_address: String, _username: String = "") -> Error:
	api.multiplayer_peer = session.get_client_peer()
	print("Local loopback client ready.")
	return OK

func poll(dt: float) -> void:
	if session:
		session.poll()
	super.poll(dt)
