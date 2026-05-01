## Container node representing a single game lobby (server or client variant).
##
## Created by [MultiplayerLobbyManager] via its spawn function. Holds the instantiated
## level scene and wires up spawn/despawn signals to the [LobbySynchronizer].
class_name Lobby
extends Node

## The [LobbySynchronizer] that manages peer visibility for this lobby.
@export var synchronizer: LobbySynchronizer

## The instantiated level scene for this lobby.
##
## Setting this property adds the level as a child, names the lobby, and hooks spawn signals.
var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self

var _context: NetwContext
## Active [NetwLobbyReadiness] gates registered via [method NetwLobbyContext.create_readiness_gate].
var _readiness_gates: Array[WeakRef] = []


## Returns the [NetwContext] for this lobby, creating it on first access.
func get_context() -> NetwContext:
	if not _context or not _context.is_valid():
		var mt := MultiplayerTree.for_node(self)
		if not mt:
			Netw.dbg.error("Lobby.get_context(): MultiplayerTree not found.", func(m): push_error(m))
			return null
		var lobby_ctx := NetwLobbyContext.new(self)
		_context = NetwContext.new(mt, lobby_ctx)
	return _context


## Connects all root-level [MultiplayerSpawner]s in [param level] to the [member synchronizer].
func hook_spawn_signals(level: Node) -> void:
	var spawners := get_spawners(level)
	for spawner in spawners:
		spawner.spawned.connect(synchronizer._on_spawned)
		spawner.despawned.connect(synchronizer._on_despawned)


## Returns all [MultiplayerSpawner]s within the [param node]'s hierarchy.
func get_spawners(node: Node) -> Array[MultiplayerSpawner]:
	var spawners: Array[MultiplayerSpawner] = []
	spawners.assign(node.find_children("*", "MultiplayerSpawner"))
	return spawners


## Registers [param player] with the synchronizer and adds it to the level scene.
func add_player(player: Node) -> void:
	synchronizer.track_player(player)
	level.add_child(player)
	player.owner = level


# ---------------------------------------------------------------------------
# Readiness gate helpers
# ---------------------------------------------------------------------------

## Registers a [NetwLobbyReadiness] gate to receive peer join/leave and
## readiness-change updates. Called internally by [NetwLobbyContext].
func _register_readiness_gate(gate: NetwLobbyReadiness) -> void:
	_cleanup_dead_gates()
	_readiness_gates.append(weakref(gate))


## Applies a readiness change from the server and broadcasts to all peers.
## Called directly when the server/host calls [method NetwLobbyReadiness.set_ready].
func _handle_set_ready(peer_id: int, is_ready: bool) -> void:
	_rpc_receive_ready_changed.rpc(peer_id, is_ready)


## Notifies all registered gates that a player entered the lobby.
## Called by [NetwLobbyContext] from [code]_on_spawned[/code].
func _notify_gates_player_added(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwLobbyReadiness
		if is_instance_valid(gate):
			gate._add_peer(peer_id)


## Notifies all registered gates that a player left the lobby.
## Called by [NetwLobbyContext] from [code]_on_despawned[/code].
func _notify_gates_player_removed(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwLobbyReadiness
		if is_instance_valid(gate):
			gate._remove_peer(peer_id)
	_cleanup_dead_gates()


func _cleanup_dead_gates() -> void:
	_readiness_gates = _readiness_gates.filter(
		func(wr: WeakRef) -> bool: return is_instance_valid(wr.get_ref())
	)


# ---------------------------------------------------------------------------
# RPCs — pause / unpause  (hard, SceneTree-level, broadcast to all peers)
# ---------------------------------------------------------------------------

## Broadcast by the server to pause the game on every peer.
##
## Uses [code]call_local[/code] so the server pauses itself in the same pass.
## Calls [code]get_tree().paused = true[/code] on each peer, which respects
## [constant Node.PROCESS_MODE_ALWAYS] nodes (e.g. pause menus).
@rpc("authority", "call_local", "reliable")
func _rpc_receive_pause(reason: String) -> void:
	get_tree().paused = true
	get_context().paused.emit(reason)


## Broadcast by the server to unpause the game on every peer.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_unpause() -> void:
	get_tree().paused = false
	get_context().unpaused.emit()


# ---------------------------------------------------------------------------
# RPCs — suspend / resume  (soft, signal-only, game code decides what to do)
# ---------------------------------------------------------------------------

## Sent by the server to notify all clients that the lobby has been suspended.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_suspend(reason: String) -> void:
	get_context().suspended.emit(reason)


## Sent by the server to notify all clients that the lobby has been resumed.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_resume() -> void:
	get_context().resumed.emit()


## Sent by a client to ask the server to suspend the lobby.
## The server emits [signal NetwContext.suspend_requested]; game code decides
## whether to honour the request by calling [method NetwContext.suspend].
@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_suspend(reason: String) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	get_context().suspend_requested.emit(peer_id, reason)


# ---------------------------------------------------------------------------
# RPCs — kick
# ---------------------------------------------------------------------------

## Sent by the server to a specific peer to inform them they are being kicked.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_kicked(reason: String) -> void:
	get_context().kicked.emit(reason)


## Sent by a client to ask the server to kick another peer.
## The server emits [signal NetwContext.kick_requested]; game code decides
## whether to honour the request by calling [method NetwContext.kick].
@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_kick(target_peer_id: int, reason: String) -> void:
	var requester_id := multiplayer.get_remote_sender_id()
	get_context().kick_requested.emit(requester_id, target_peer_id, reason)


# ---------------------------------------------------------------------------
# RPCs — countdown
# ---------------------------------------------------------------------------

## Sent by the server when a new countdown starts.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_countdown_started(seconds: int) -> void:
	get_context().countdown_started.emit(seconds)


## Sent by the server on each countdown tick.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_countdown_tick(seconds_left: int) -> void:
	get_context().countdown_tick.emit(seconds_left)


## Sent by the server when the countdown reaches zero.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_countdown_finished() -> void:
	get_context().countdown_finished.emit()


## Sent by the server when a running countdown is cancelled.
@rpc("authority", "call_remote", "reliable")
func _rpc_receive_countdown_cancelled() -> void:
	get_context().countdown_cancelled.emit()


# ---------------------------------------------------------------------------
# RPCs — readiness
# ---------------------------------------------------------------------------

## Sent by a client to report their ready state to the server.
@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_set_ready(is_ready: bool) -> void:
	var peer_id := multiplayer.get_remote_sender_id()
	_handle_set_ready(peer_id, is_ready)


## Broadcast by the server to synchronise a readiness change on all peers.
## [code]call_local[/code] ensures the server's own gates are also updated.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_ready_changed(peer_id: int, is_ready: bool) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwLobbyReadiness
		if is_instance_valid(gate):
			gate._receive_ready_changed(peer_id, is_ready)
	_cleanup_dead_gates()
