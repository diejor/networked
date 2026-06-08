## Thin facade over [MultiplayerTree] exposing only session-level APIs.
##
## Components obtain this via [member NetwContext.tree] rather than
## holding a direct reference to [MultiplayerTree]. This keeps component code
## off the concrete [MultiplayerTree] class (backend, multiplayer_api, etc.).
##
## [br][br]
## Holds a [WeakRef] so components that cache an instance survive tree
## teardown without keeping the [MultiplayerTree] alive.
class_name NetwTree
extends RefCounted

## Emitted when a new peer connects to the server.
signal peer_connected(peer_id: int)
## Emitted when a peer disconnects from the server.
signal peer_disconnected(peer_id: int)
## Emitted on the client when it successfully connects to the server.
signal connected_to_server()
## Emitted on the client when the server disconnects or crashes.
signal server_disconnected()

## Emitted on every peer after the server accepts a player join.
signal player_joined(rj: ResolvedJoin)
## Emitted when this peer's player join has been accepted by the server.
signal local_player_joined(rj: ResolvedJoin)
## Emitted after a player's target scene has been activated.
signal player_scene_ready(rj: ResolvedJoin, netw_scene: NetwScene)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on every peer when the game is paused via [method pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via [method unpause].
signal tree_unpaused()
## Emitted when the session reaches [constant MultiplayerTree.ONLINE] and
## services are ready. Pairs with [signal session_ended].
signal session_entered()
## Emitted when the session leaves [constant MultiplayerTree.ONLINE] and
## tears down. Pairs with [signal session_entered].
signal session_ended()

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)
	mt.peer_connected.connect(peer_connected.emit)
	mt.peer_disconnected.connect(peer_disconnected.emit)
	mt.connected_to_server.connect(connected_to_server.emit)
	mt.server_disconnected.connect(server_disconnected.emit)

	mt.player_joined.connect(player_joined.emit)
	mt.local_player_joined.connect(local_player_joined.emit)
	mt.player_scene_ready.connect(_on_player_scene_ready)
	mt.server_disconnecting.connect(server_disconnecting.emit)
	mt.kick_requested.connect(kick_requested.emit)
	mt.kicked.connect(kicked.emit)
	mt.tree_paused.connect(tree_paused.emit)
	mt.tree_unpaused.connect(tree_unpaused.emit)
	mt.session_entered.connect(session_entered.emit)
	mt.session_ended.connect(session_ended.emit)


## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return is_instance_valid(_tree_ref.get_ref())


## Returns an array of all active player nodes across all scenes or the
## sceneless world.
func get_all_players() -> Array[Node]:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_all_players() if mt else []


## Returns accepted player join data known by this peer.
func get_joined_players() -> Array[ResolvedJoin]:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_joined_players() if mt else []


## Returns the accepted player data for [param peer_id], or
## [code]null[/code].
func get_joined_player(peer_id: int) -> ResolvedJoin:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_joined_player(peer_id) if mt else null


## Returns [code]true[/code] if the current session is hosting as a server.
func is_server() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.is_host if mt else false


## Returns [code]true[/code] if this tree is acting as a listen-server host.
##
## Use this as the single source of truth for all listen-server checks
## instead of comparing [member MultiplayerTree.role] directly.
func is_listen_server() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.role == MultiplayerTree.Role.LISTEN_SERVER if mt else false


## Returns the original name of the [MultiplayerTree] node.
func get_tree_name() -> String:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_tree_name() if mt else ""


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.is_online() if mt else false


## Starts the instance as a network host using [param join_payload].
##
## Use this when the caller knows they are hosting; otherwise see
## [method auto_connect_player].
func host_player(join_payload: JoinPayload) -> Error:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return await mt.host_player(join_payload) if mt else ERR_UNCONFIGURED


## Opens the transport against the [param target] address and submits
## [param join_payload] once connected.
##
## See [method MultiplayerTree.join].
func join(
		target: JoinTarget,
		join_payload: JoinPayload,
		timeout: float = 5.0,
		quiet: bool = false,
) -> Error:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return ERR_UNCONFIGURED
	return await mt.join(target, join_payload, timeout, quiet)


## Probes the target address; joins if reachable, hosts otherwise.
##
## See [method MultiplayerTree.join_or_host].
func join_or_host(
		target: JoinTarget,
		join_payload: JoinPayload,
) -> Error:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return ERR_UNCONFIGURED
	return await mt.join_or_host(target, join_payload)


## Returns the tree's configured [BackendPeer], or [code]null[/code].
##
## Exposed so callers can pass the existing backend to [method join]
## or [method join_or_host] without holding a direct
## [MultiplayerTree] reference.
func get_backend() -> BackendPeer:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.backend if mt else null


## Returns the current connection state.
func get_state() -> MultiplayerTree.State:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.state if mt else MultiplayerTree.State.OFFLINE


## Returns the current role in the session.
func get_role() -> MultiplayerTree.Role:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.role if mt else MultiplayerTree.Role.NONE


## Returns the local player node for this tree, or [code]null[/code].
func get_local_player() -> Node:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.local_player if mt else null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## The pause is sent to each connected peer individually.
## [br][br][b]Server Only.[/b]
func pause(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.pause() must be called on the server.")
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_pause.rpc_id(peer_id, reason)
	mt._rpc_receive_pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.unpause() must be called on the server.")
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_unpause.rpc_id(peer_id)
	mt._rpc_receive_unpause()


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.kick() must be called on the server.")
	if not reason.is_empty():
		mt._rpc_receive_kicked.rpc_id(peer_id, reason)
	if mt.multiplayer_peer:
		mt.multiplayer_peer.disconnect_peer(peer_id)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt._rpc_request_kick.rpc_id(1, peer_id, reason)


## Saves game state, closes the multiplayer peer, and waits for the server
## to acknowledge disconnection.
func disconnect_player() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	await mt.disconnect_player()


## Asks the server for permission to disconnect.
##
## The server decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_disconnect(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt._rpc_request_disconnect.rpc_id(1, reason)


## Notifies all clients that the server is shutting down.
##
## Clients receive [signal server_disconnecting].
## [br][br][b]Server Only.[/b]
func notify_disconnect(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(
		mt.is_server,
		"NetwTree.notify_disconnect() must be called on the server.",
	)
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_notify_disconnect.rpc_id(peer_id, reason)
	mt._rpc_receive_notify_disconnect.rpc_id(1, reason)


func _on_player_scene_ready(
		rj: ResolvedJoin,
		scene: MultiplayerScene,
) -> void:
	var netw_scene := NetwScene.new(scene) if is_instance_valid(scene) else null
	player_scene_ready.emit(rj, netw_scene)
	if is_instance_valid(scene):
		scene.player_ready.emit(rj)
