## Thin facade over [MultiplayerTree] exposing only session-service access.
##
## Components obtain this via [member NetwContext.session] rather than
## holding a direct reference to [MultiplayerTree]. This keeps component code
## off the concrete [MultiplayerTree] class (backend, multiplayer_api, etc.)
## while preserving the existing service API unchanged.
## [br][br]
## Holds a [WeakRef] so components that cache an instance survive tree
## teardown without keeping the [MultiplayerTree] alive.
class_name NetwSessionContext
extends RefCounted

var _mt_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_mt_ref = weakref(mt)


## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return is_instance_valid(_mt_ref.get_ref())


## Returns the [MultiplayerLobbyManager] service, or [code]null[/code].
func get_lobby_manager() -> MultiplayerLobbyManager:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(MultiplayerLobbyManager)


## Returns the [NetworkClock] service, or [code]null[/code].
func get_clock() -> NetworkClock:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(NetworkClock)


## Returns the [NetwPeerContext] for [param peer_id].
func get_peer_context(peer_id: int) -> NetwPeerContext:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_peer_context(peer_id) if mt else null


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_service(type) if mt else null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(
	spawner_path: SceneNodePath
) -> MultiplayerTree.SpawnSlot:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return MultiplayerTree.SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Returns an array of all active player nodes across all lobbies or the
## lobbyless world.
func get_all_players() -> Array[Node]:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_all_players() if mt else []


## Returns [code]true[/code] if the current session is hosting as a server.
func is_server() -> bool:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.is_server if mt else false


## Returns the unique peer ID for this session.
func get_unique_id() -> int:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if mt and mt.multiplayer_api:
		return mt.multiplayer_api.get_unique_id()
	return 0


## Returns the original name of the [MultiplayerTree] node.
func get_tree_name() -> String:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_tree_name() if mt else ""


## Begins a [NetSpan] for tracing.
func begin_span(
	label: String,
	meta: Dictionary = {},
	follows_from: CheckpointToken = null
) -> NetSpan:
	return Netw.dbg.span(_mt_ref.get_ref(), label, meta, follows_from)


## Begins a [NetPeerSpan] for tracing multiple peers.
func begin_peer_span(
	label: String,
	peers: Array = [],
	meta: Dictionary = {},
	follows_from: CheckpointToken = null
) -> NetPeerSpan:
	return Netw.dbg.peer_span(
		_mt_ref.get_ref(),
		label,
		peers,
		meta,
		follows_from
	)
