## [BackendPeer] that routes packets through an in-process [LocalLoopbackSession].
##
## Used automatically by [MultiplayerTree] when running on the web with a non-WebRTC backend,
## ensuring a fast, allocation-free loopback without any real network sockets.
@tool
class_name LocalLoopbackBackend
extends BackendPeer

## The shared in-process loopback session.
var session: LocalLoopbackSession = null


func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("LocalLoopbackBackend: create_host_peer called.")
	if not session:
		session = LocalLoopbackSession.get_shared_session()

	if not session.has_live_server():
		session.reset()
	Netw.dbg.info("Local loopback server ready.")
	return session.get_server_peer()


func create_join_peer(
	_tree: MultiplayerTree, _server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace("LocalLoopbackBackend: create_join_peer called.")
	if not session:
		session = LocalLoopbackSession.get_shared_session()

	if not session.has_live_server():
		Netw.dbg.warn("Local loopback: no live server to join.",
		func(m): push_warning(m))
		return null

	Netw.dbg.info("Local loopback client ready.")
	return session.create_client_peer()


func poll(_dt: float) -> void:
	if session:
		session.poll()


func _copy_from(source: BackendPeer) -> void:
	session = (source as LocalLoopbackBackend).session


func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	return []
