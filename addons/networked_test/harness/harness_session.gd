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
	HOST,
	OPEN_HOST,
}


## Transport seam for harness session entry.
##
## Implementations build a [NetwConnectTarget], and tear down
## transport state associated with a [MultiplayerTree].
class BackendAdapter:
	## Builds a [NetwConnectTarget] for [param tree] and [param address].
	func make_connect_target(
			tree: MultiplayerTree,
			address: String = "",
	) -> NetwConnectTarget:
		assert(false, "BackendAdapter.make_connect_target must be implemented.")
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


	## Builds a [NetwConnectTarget] for [param tree]'s local loopback backend.
	func make_connect_target(
			tree: MultiplayerTree,
			address: String = "",
	) -> NetwConnectTarget:
		var target := NetwConnectTarget.new()
		target.scheme = tree.scheme
		target.address = address if not address.is_empty() else "localhost"
		return target


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


## Applies local loopback defaults and configures local scheme.
func adopt_tree(
		tree: MultiplayerTree,
		role: NetwMultiplayer.Role,
) -> void:
	tree.desired_role = role
	tree.auto_host_headless = false
	tree.debug_join = null
	tree.transport = NetwLocalParams.new()
	LocalLoopbackSession.set_shared_session(_session)


## Builds a [NetwConnectTarget] for [param tree]'s local loopback backend.
func make_connect_target(
		tree: MultiplayerTree,
		address: String = "",
) -> NetwConnectTarget:
	return _adapter.make_connect_target(tree, address)


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
			return NetwConnector.error_of(
				await NetwConnector.of(tree.api).join(
					active_adapter.make_connect_target(tree),
					payload,
				),
			)
		Entry.JOIN_OR_HOST:
			return NetwConnector.error_of(
				await NetwConnector.of(tree.api).join_or_host(
					active_adapter.make_connect_target(tree),
					payload,
				),
			)
		Entry.HOST:
			return await tree.host(payload)
		Entry.OPEN_HOST:
			return NetwConnector.error_of(
				await NetwConnector.of(tree.api).host(null),
			)
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
	tree.state = NetwMultiplayer.SessionState.DISCONNECTING
	if peer:
		_session.release_inbound_packets(peer)
	tree.multiplayer_peer.close()
	return peer_id


## Builds a [JoinPayload] for [param username] and [param spawn] intent, where
## [param spawn] is a [SceneNodePath] template, a ready [JoinPayload] whose args
## are copied, or an [Array] of typed join args.
func build_join_payload(
		username: String,
		spawn: Variant = null,
) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	if spawn is JoinPayload:
		payload.arg_values = (spawn as JoinPayload).arg_values.duplicate(true)
	elif spawn is SceneNodePath:
		payload.arg_values = NetwDefaultJoin.args_from_scene_node_path(spawn)
	elif spawn is Array:
		payload.arg_values = (spawn as Array).duplicate(true)
	return payload


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
