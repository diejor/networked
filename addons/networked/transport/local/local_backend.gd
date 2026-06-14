## [BackendPeer] that routes packets through an in process
## [LocalLoopbackSession].
##
## Used automatically by [MultiplayerTree] when running on the web with a
## non WebRTC backend, ensuring a fast, allocation free loopback without any
## real network sockets.
@tool
class_name LocalLoopbackBackend
extends BackendPeer

## Shared local loopback session.
var session: LocalLoopbackSession = null


## Implements [method BackendPeer.create_host_peer] with [member session].
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("LocalLoopbackBackend: create_host_peer called.")
	if not session:
		session = LocalLoopbackSession.get_shared_session()

	if not session.has_live_server():
		session.reset()
	session.server_app_id = _tree.app_id if _tree else &""
	Netw.dbg.info("Local loopback server ready.")
	return session.get_server_peer()


## Implements [method BackendPeer.create_join_peer] with [member session].
func create_join_peer(
		_tree: MultiplayerTree,
		_server_address: String,
		_username: String = "",
) -> MultiplayerPeer:
	Netw.dbg.trace("LocalLoopbackBackend: create_join_peer called.")
	if not session:
		session = LocalLoopbackSession.get_shared_session()

	if not session.has_live_server():
		Netw.dbg.warn(
			"Local loopback: no live server to join.",
			func(m): push_warning(m)
		)
		return null

	Netw.dbg.info("Local loopback client ready.")
	return session.create_client_peer()


## Implements [method BackendPeer.poll] by polling [member session].
func poll(_dt: float) -> void:
	if session:
		session.poll_frame_scoped()


## Implements [method BackendPeer.query_server_info] from [member session].
##
## No probe connection is needed because loopback clients and servers share
## [LocalLoopbackSession].
func query_server_info(
		_address: String,
		_timeout: float = 2.0,
) -> ServerInfoResult:
	if not session:
		session = LocalLoopbackSession.get_shared_session()
	if session.has_live_server():
		var info := ServerInfo.new()
		info.is_local_listener = true
		info.players = session.server_peer.linked_peers.size()
		info.app_id = session.server_app_id
		return ServerInfoResult.ok(info)
	return ServerInfoResult.unsupported()


## Returns an [AddressHint] that hides address input.
func get_address_hint() -> AddressHint:
	var hint := AddressHint.make(
		"",
		"",
		"In-process loopback. No address required.",
		true,
		true,
	)
	hint.hides_address_field = true
	return hint


## Preserves [member session] after [method Resource.duplicate].
func copy_from(source: BackendPeer) -> void:
	session = (source as LocalLoopbackBackend).session


## Returns the display name for this backend.
func get_display_name() -> String:
	return "Local"
