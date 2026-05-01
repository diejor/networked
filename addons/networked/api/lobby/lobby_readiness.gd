## Per-lobby readiness gate — tracks which players have confirmed ready.
##
## Obtain via [method NetwLobbyContext.create_readiness_gate].
## Clients call [method set_ready]; the server broadcasts the change to all peers.
## [codeblock]
## # Game lobby screen (runs on all peers):
## var gate := ctx.create_readiness_gate()
## gate.player_ready_changed.connect(_refresh_ready_ui)
## gate.all_ready.connect(_on_everyone_ready)
##
## # Player clicks "Ready":
## gate.set_ready(true)
## [/codeblock]
class_name NetwLobbyReadiness
extends RefCounted

## Emitted on all peers when a player's readiness state changes.
signal player_ready_changed(peer_id: int, is_ready: bool)
## Emitted when every tracked player is ready.
##
## [b]Note:[/b] Also emits when a not-ready player leaves, if the remaining
## players are all ready.
signal all_ready()

var _lobby_ref: WeakRef
## Peer ID → ready state. Populated as players enter/leave the lobby.
var _readiness: Dictionary[int, bool] = {}


func _init(lobby: Lobby) -> void:
	_lobby_ref = weakref(lobby)


## Returns [code]true[/code] while the underlying [Lobby] is still alive.
func is_valid() -> bool:
	return is_instance_valid(_lobby_ref.get_ref())


## Returns [code]true[/code] if [param peer_id] has confirmed ready.
func is_peer_ready(peer_id: int) -> bool:
	return _readiness.get(peer_id, false)


## Returns all peer IDs that have confirmed ready.
func get_ready_peers() -> Array[int]:
	var result: Array[int] = []
	for id: int in _readiness:
		if _readiness[id]:
			result.append(id)
	return result


## Returns [code]true[/code] when every tracked player is ready and there is at
## least one player.
func are_all_ready() -> bool:
	if _readiness.is_empty():
		return false
	for v: bool in _readiness.values():
		if not v:
			return false
	return true


## Marks the local player as ready (or not ready).
##
## On a client this sends an RPC to the server; on the server/host it applies
## the change directly. The update is broadcast to all peers automatically.
func set_ready(ready: bool = true) -> void:
	var lobby := _lobby_ref.get_ref() as Lobby
	if not is_instance_valid(lobby):
		return
	if lobby.multiplayer.is_server():
		lobby._handle_set_ready(lobby.multiplayer.get_unique_id(), ready)
	else:
		lobby._rpc_request_set_ready.rpc_id(1, ready)


## Called internally by [Lobby] when the server broadcasts a readiness update.
func _receive_ready_changed(peer_id: int, is_ready: bool) -> void:
	_readiness[peer_id] = is_ready
	player_ready_changed.emit(peer_id, is_ready)
	if are_all_ready():
		all_ready.emit()


## Called internally when a player enters the lobby (starts as not-ready).
func _add_peer(peer_id: int) -> void:
	if peer_id not in _readiness:
		_readiness[peer_id] = false


## Called internally when a player leaves the lobby (removes their entry).
func _remove_peer(peer_id: int) -> void:
	if _readiness.erase(peer_id) and are_all_ready():
		all_ready.emit()
