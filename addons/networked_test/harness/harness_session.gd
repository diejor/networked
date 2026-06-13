## Shared session plumbing for Networked test harnesses.
##
## [member session] owns the in process transport. Harnesses keep their own
## tree construction and public flow APIs, then delegate backend, join
## payload, and link condition plumbing here.
class_name NetwHarnessSession
extends RefCounted

## Session entry method used by [method connect_tree].
enum Entry {
	JOIN,
	JOIN_OR_HOST,
	HOST_PLAYER,
	HOST,
}


## Transport seam for harness session entry.
##
## Implementations build a [BackendPeer], build a [JoinTarget], and tear down
## transport state associated with a [MultiplayerTree].
class BackendAdapter:
	## Builds a backend template for a [MultiplayerTree].
	func make_backend() -> BackendPeer:
		assert(false, "BackendAdapter.make_backend must be implemented.")
		return null


	## Builds a [JoinTarget] for [param tree] and [param address].
	func make_join_target(
			tree: MultiplayerTree,
			address: String = "",
	) -> JoinTarget:
		assert(false, "BackendAdapter.make_join_target must be implemented.")
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


	## Builds a [LocalLoopbackBackend] wired to the harness session.
	func make_backend() -> BackendPeer:
		var backend := LocalLoopbackBackend.new()
		backend.session = _session
		return backend


	## Builds a [JoinTarget] for [param tree]'s local loopback backend.
	func make_join_target(
			tree: MultiplayerTree,
			address: String = "",
	) -> JoinTarget:
		var target := JoinTarget.new()
		target.backend = tree.backend
		target.address = address if not address.is_empty() \
		else tree.backend.get_join_address()
		return target


var _session: LocalLoopbackSession = LocalLoopbackSession.new()
var _adapter: BackendAdapter = LoopbackAdapter.new(_session)


## Returns the [LocalLoopbackSession] used by this harness session.
func session() -> LocalLoopbackSession:
	return _session


## Resets the owned [LocalLoopbackSession].
func reset() -> void:
	if _session:
		_session.reset()


## Builds a [LocalLoopbackBackend] wired to [method session].
func make_backend() -> LocalLoopbackBackend:
	return _adapter.make_backend() as LocalLoopbackBackend


## Applies local loopback defaults and installs [method make_backend].
func adopt_tree(
		tree: MultiplayerTree,
		role: MultiplayerTree.Role,
) -> void:
	tree.desired_role = role
	tree.auto_host_headless = false
	tree.debug_join = null
	tree.backend = make_backend()


## Builds a [JoinTarget] for [param tree]'s local loopback backend.
func make_join_target(
		tree: MultiplayerTree,
		address: String = "",
) -> JoinTarget:
	return _adapter.make_join_target(tree, address)


## Connects [param tree] through [param entry].
##
## [param adapter] builds the join target for entries that require one. When
## omitted, the loopback adapter owned by this session is used.
func connect_tree(
		tree: MultiplayerTree,
		entry: Entry,
		payload: JoinPayload = null,
		adapter: BackendAdapter = null,
) -> Error:
	var active_adapter := adapter if adapter else _adapter
	match entry:
		Entry.JOIN:
			return await tree.join(
				active_adapter.make_join_target(tree),
				payload,
			)
		Entry.JOIN_OR_HOST:
			return await tree.join_or_host(
				active_adapter.make_join_target(tree),
				payload,
			)
		Entry.HOST_PLAYER:
			return await tree.host_player(payload)
		Entry.HOST:
			return await tree.host()
		_:
			return ERR_INVALID_PARAMETER


## Takes [param tree] offline. Marks it
## [constant MultiplayerTree.DISCONNECTING], flushes held inbound
## packets, and closes the peer. Returns the closed peer id so callers can
## await server unregistration. Inverse of [method connect_tree].
func disconnect_tree(tree: MultiplayerTree) -> int:
	if not tree.multiplayer_peer:
		return 0

	var peer := tree.multiplayer_peer as LocalMultiplayerPeer
	var peer_id := tree.multiplayer_peer.get_unique_id()
	tree.state = MultiplayerTree.State.DISCONNECTING
	if peer:
		_session.release_inbound_packets(peer)
	tree.multiplayer_peer.close()
	return peer_id


## Builds a [JoinPayload] for [param username] and [param spawn].
func build_join_payload(
		username: String,
		spawn: Variant = null,
) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	if spawn is JoinPayload:
		payload.spawn = spawn.spawn
	elif spawn is SceneNodePath:
		payload.spawn = EntitySpawnPolicy.from_scene_node_path(spawn).to_dict()
	elif spawn is SpawnPolicy:
		payload.spawn = spawn.to_dict()
	elif spawn is Dictionary:
		payload.spawn = spawn
	return payload


## Sets inbound link conditions on [param peer].
func set_link_conditions(
		peer: LocalMultiplayerPeer,
		conditions: LocalLoopbackSession.LinkConditions,
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
) -> LocalLoopbackSession.LinkConditions:
	return _session.get_link_conditions(peer, sender_id)
