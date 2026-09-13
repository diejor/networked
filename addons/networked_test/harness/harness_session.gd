## Shared session plumbing for Networked test harnesses.
##
## [member session] owns the in process transport. Harnesses keep their own
## tree construction and public flow APIs, then delegate backend, join
## payload, and link condition plumbing here.
class_name NetwHarnessSession
extends RefCounted

## How long [method connect_tree] waits for the session to come online, in
## milliseconds. It is the harness's own patience, not a session deadline.
const ONLINE_TIMEOUT_MS := 5000

## Which side of the backend [method connect_tree] asks for.
enum Entry {
	JOIN,
	HOST,
}


## Peer seam for harness session entry.
##
## Implementations build the [MultiplayerPeer] a tree assigns for one
## [enum NetwHarnessSession.Entry], and tear down transport state associated
## with a [MultiplayerTree]. Nothing here connects: [method connect_tree] owns
## preparation and assignment, so every harness enters a session the way a game
## does.
class BackendAdapter:
	## Builds the peer [param tree] assigns to enter as [param entry].
	func make_peer(
			tree: MultiplayerTree,
			entry: Entry,
	) -> MultiplayerPeer:
		assert(false, "BackendAdapter.make_peer must be implemented.")
		return null


	## Releases transport resources for a tree.
	func teardown(
			_tree: MultiplayerTree,
			_scene_tree: SceneTree,
	) -> void:
		pass


## Loopback implementation of [NetwHarnessSession.BackendAdapter].
##
## [method teardown] is a no-op because loopback release is session-wide
## through [method NetwHarnessSession.reset].
class LoopbackAdapter:
	extends BackendAdapter

	var _session: LocalLoopbackSession


	func _init(session: LocalLoopbackSession) -> void:
		_session = session


	## Answers the shared loopback server peer for
	## [constant NetwHarnessSession.Entry.HOST] and a fresh client peer,
	## already handshaken against it, for
	## [constant NetwHarnessSession.Entry.JOIN].
	func make_peer(
			_tree: MultiplayerTree,
			entry: Entry,
	) -> MultiplayerPeer:
		if entry == Entry.HOST:
			return _session.get_server_peer()
		return _session.create_client_peer()


var _session: LocalLoopbackSession = LocalLoopbackSession.new()
var _adapter: BackendAdapter = LoopbackAdapter.new(_session)


## Returns the [LocalLoopbackSession] used by this harness session.
func session() -> LocalLoopbackSession:
	return _session


## Resets the owned [LocalLoopbackSession].
##
## Clears the process-global session when it still points at this session, so
## a torn-down harness leaves no shared pointer for the next test to inherit.
func reset() -> void:
	if _session:
		_session.reset()
	if LocalLoopbackSession.has_shared_session() \
			and LocalLoopbackSession.get_shared_session() == _session:
		LocalLoopbackSession.set_shared_session(null)


## Applies local loopback defaults and names the loopback peer class.
func adopt_tree(
		tree: MultiplayerTree,
		role: NetwMultiplayer.Role,
) -> void:
	tree.desired_role = role
	tree.auto_host_headless = false
	tree.debug_join = null
	tree.peer_class = &"LocalMultiplayerPeer"
	tree.transport_settings = { }
	LocalLoopbackSession.set_shared_session(_session)


## Brings [param tree] online as [param entry], the ordinary way.
##
## [param adapter] supplies the peer. When omitted, the loopback adapter owned
## by this session is used. A non-empty [param username] is prepared through
## [method NetwMultiplayer.session_prepare_join] BEFORE the assignment, so a
## client holds its hello and a host submits once its startup scenes exist.
## Returns when the session is online, which is the condition a caller waiting
## on entry actually means.
##
## A direct peer assignment is refused while a declaration is still being
## authored, so this waits one frame for the deferred configuration to settle
## before assigning.
func connect_tree(
		tree: MultiplayerTree,
		entry: Entry,
		username: StringName = &"",
		join_args: Array = [],
		adapter: BackendAdapter = null,
) -> Error:
	var active_adapter := adapter if adapter else _adapter
	var peer := active_adapter.make_peer(tree, entry)
	if peer == null:
		return ERR_CANT_CREATE
	var scene_tree := tree.get_tree()
	if scene_tree != null:
		await scene_tree.process_frame
	if not String(username).is_empty():
		tree.api.session_prepare_join(username, join_args)
	tree.api.multiplayer_peer = peer
	if tree.api.multiplayer_peer != peer:
		return ERR_CANT_CONNECT
	return await _await_online(tree)


func _await_online(tree: MultiplayerTree) -> Error:
	var deadline := Time.get_ticks_msec() + ONLINE_TIMEOUT_MS
	var scene_tree := tree.get_tree()
	while not tree.api.is_online:
		if Time.get_ticks_msec() > deadline or scene_tree == null:
			return ERR_TIMEOUT
		await scene_tree.process_frame
	return OK


## Takes [param tree] offline. Marks it
## [constant MultiplayerTree.DISCONNECTING], flushes held inbound
## packets, and closes the peer. Returns the closed peer id so callers can
## await server unregistration. Inverse of [method connect_tree].
func disconnect_tree(tree: MultiplayerTree) -> int:
	if not tree.api.multiplayer_peer:
		return 0

	var peer := tree.api.multiplayer_peer as LocalMultiplayerPeer
	var peer_id := tree.api.multiplayer_peer.get_unique_id()
	tree.api.session_set_state(NetwMultiplayer.SESSION_STATE_DISCONNECTING)
	if peer:
		_session.release_inbound_packets(peer)
	tree.api.multiplayer_peer.close()
	return peer_id


## Builds the join args for a [param spawn] intent, where [param spawn] is an
## [Array] of typed join args or [code]null[/code].
func build_join_args(spawn: Variant = null) -> Array:
	if spawn is Array:
		return (spawn as Array).duplicate(true)
	return []


## Sets inbound link conditions on [param peer].
func set_link_conditions(
		peer: LocalMultiplayerPeer,
		conditions: LocalLinkConditions,
		sender_id: int = 0,
) -> void:
	_session.set_link_conditions(peer, conditions, sender_id)


## Clears inbound link conditions on [param peer] and [param sender_id].
func clear_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> void:
	_session.clear_link_conditions(peer, sender_id)


## Returns inbound link conditions for [param peer].
func get_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> LocalLinkConditions:
	return _session.get_link_conditions(peer, sender_id)
