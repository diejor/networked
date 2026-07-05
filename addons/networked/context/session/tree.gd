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

## Emitted once for each accepted participant known to this peer.
##
## Fresh accepts emit on every peer. Late joiners also receive one emission per
## participant accepted before they connected.
signal participant_joined(participant: NetwParticipant)
## Emitted when this peer's participant has been accepted by the server.
signal local_participant_joined(participant: NetwParticipant)
## Emitted when [member local_participant] changes [member NetwParticipant.current_scene].
signal local_scene_changed(from: NetwScene, to: NetwScene)
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

	mt.participant_joined.connect(participant_joined.emit)
	mt.local_participant_joined.connect(local_participant_joined.emit)
	mt.local_scene_changed.connect(local_scene_changed.emit)
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

## All active player identities across all scenes or the sceneless world.
var all_players: Array[NetwEntity]:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.get_all_players() if mt else []

## Accepted participants known by this peer.
var participants: Array[NetwParticipant]:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.get_participants() if mt else []


## Returns the [NetwParticipant] for [param peer_id], or [code]null[/code].
func participant(peer_id: int) -> NetwParticipant:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_participant(peer_id) if mt else null


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

## The original name of the [MultiplayerTree] node.
var tree_name: String:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.get_tree_name() if mt else ""


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.is_online() if mt else false


## Starts the instance as a network host using [param join_payload].
##
## Use this when the caller knows they are hosting. Otherwise, see
## [method auto_connect_player].
func host(join_payload: JoinPayload) -> Error:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return await mt.host(join_payload) if mt else ERR_UNCONFIGURED


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


## Probes the target address. Joins if reachable, hosts otherwise.
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

## The tree's configured [BackendPeer], or [code]null[/code].
##
## Exposed so callers can pass the existing backend to [method join]
## or [method join_or_host] without holding a direct
## [MultiplayerTree] reference.
var backend: BackendPeer:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.backend if mt else null

## The current connection state.
var state: MultiplayerTree.State:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.state if mt else MultiplayerTree.State.OFFLINE

## The current role in the session.
var role: MultiplayerTree.Role:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.role if mt else MultiplayerTree.Role.NONE

## The local player identity for this tree, or [code]null[/code].
var local_player: NetwEntity:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.local_player if mt else null

## Accepted [NetwParticipant] for this tree, or [code]null[/code].
var local_participant: NetwParticipant:
	get:
		var mt := _tree_ref.get_ref() as MultiplayerTree
		return mt.local_participant if mt else null


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
	if mt:
		mt.pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [br][br][b]Server Only.[/b]
func unpause() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt:
		mt.unpause()


## Disconnects [param peer_id] from the session.
##
## If [param reason] is non-empty, the peer receives [signal kicked] before
## the connection is closed.
## [br][br][b]Server Only.[/b]
func kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt:
		mt.kick(peer_id, reason)


## Asks the server to kick [param peer_id].
##
## The server emits [signal kick_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt:
		mt.request_kick(peer_id, reason)


## Saves game state, closes the multiplayer peer, and waits for the server
## to acknowledge leaving.
func leave() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	await mt.leave()


## Asks the server for permission to leave.
##
## The server decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_leave(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt:
		mt.request_leave(reason)


## Notifies all clients that the server is shutting down.
##
## Clients receive [signal server_disconnecting].
## [br][br][b]Server Only.[/b]
func notify_shutdown(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt:
		mt.notify_shutdown(reason)
