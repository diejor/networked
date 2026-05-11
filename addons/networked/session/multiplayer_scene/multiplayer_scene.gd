class_name MultiplayerScene
extends Node

## Container node representing a single game scene (server or client variant).
##
## Created by [MultiplayerSceneManager] via its spawn function. Holds the instantiated
## level scene and wires up spawn/despawn signals to the [SceneSynchronizer].

## The [SceneSynchronizer] that manages peer visibility for this scene.
@export var synchronizer: SceneSynchronizer

## The instantiated level scene for this scene.
##
## Setting this property adds the level as a child, names the scene, and hooks spawn signals.
var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self

var _context: NetwContext
## Emitted when a player toggles their ready state to [code]true[/code] via
## [NetwSceneReadiness].[br][br]This is a manual ready-state signal, not an
## automatic join event. See [signal player_entered] for spawn detection.
signal player_ready(join_payload: JoinPayload)

## Active [NetwSceneReadiness] gates registered via [method NetwScene.create_readiness_gate].
var _readiness_gates: Array[WeakRef] = []
var _players_by_peer: Dictionary[int, WeakRef] = {}


## Returns the [NetwContext] for this scene, creating it on first access.
func get_context() -> NetwContext:
	if not _context or not _context.is_valid():
		var mt := MultiplayerTree.for_node(self)
		if not mt:
			Netw.dbg.error("Scene.get_context(): MultiplayerTree not found.", func(m): push_error(m))
			return null
		var scene_ctx := NetwScene.new(self)
		_context = NetwContext.new(mt, scene_ctx)
	return _context


func _on_tree_exiting() -> void:
	if _context and _context.has_scene():
		_context.scene.close()


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
	synchronizer.track_node(player)
	level.add_child(player)
	player.owner = level
	register_player(player)


func register_player(player: Node) -> void:
	var peer_id := _get_peer_id(player)
	if peer_id == 0:
		Netw.dbg.error(
			"Cannot register player '%s': peer_id is 0.",
			[player.name],
			func(m): push_error(m)
		)
		return
	var previous_peer := _find_peer_for_player(player)
	if previous_peer != 0 and previous_peer != peer_id:
		_players_by_peer.erase(previous_peer)
		synchronizer.disconnect_peer(previous_peer)
	_players_by_peer[peer_id] = weakref(player)
	if not synchronizer.connected_peers.has(peer_id):
		synchronizer.connect_peer(peer_id)
	var bound := _on_player_exiting.bind(player)
	if not player.tree_exiting.is_connected(bound):
		player.tree_exiting.connect(bound)


func get_players() -> Array[Node]:
	var players: Array[Node] = []
	var stale_peers: Array[int] = []
	for peer_id: int in _players_by_peer:
		var player := _players_by_peer[peer_id].get_ref() as Node
		if is_instance_valid(player):
			players.append(player)
		else:
			stale_peers.append(peer_id)
	for peer_id: int in stale_peers:
		_players_by_peer.erase(peer_id)
	return players


func _on_player_exiting(player: Node) -> void:
	_remove_player(player)


func _remove_player(player: Node) -> void:
	var peer_id := _get_peer_id(player)
	if peer_id == 0:
		peer_id = _find_peer_for_player(player)
	if peer_id != 0:
		_players_by_peer.erase(peer_id)
	var bound := _on_player_exiting.bind(player)
	if is_instance_valid(player) and player.tree_exiting.is_connected(bound):
		player.tree_exiting.disconnect(bound)
	if peer_id != 0:
		synchronizer.disconnect_peer(peer_id)


func _find_peer_for_player(player: Node) -> int:
	for peer_id: int in _players_by_peer:
		if _players_by_peer[peer_id].get_ref() == player:
			return peer_id
	return 0


# ---------------------------------------------------------------------------
# Readiness gate helpers
# ---------------------------------------------------------------------------

## Registers a [NetwSceneReadiness] gate to receive peer join/leave and
## readiness-change updates. Called internally by [NetwScene].
func _register_readiness_gate(gate: NetwSceneReadiness) -> void:
	_cleanup_dead_gates()
	_readiness_gates.append(weakref(gate))


## Applies a readiness change from the server and broadcasts to scene peers.
## Called directly when the server/host calls [method NetwSceneReadiness.set_ready].
func _handle_set_ready(peer_id: int, is_ready: bool) -> void:
	_rpc_receive_ready_changed(peer_id, is_ready)
	for target_peer_id: int in synchronizer.connected_peers:
		if target_peer_id != multiplayer.get_unique_id():
			rpc_id(target_peer_id, "_rpc_receive_ready_changed", peer_id, is_ready)


## Notifies all registered gates that a player entered the scene.
## Called by [NetwScene] from [code]_on_spawned[/code].
func _notify_gates_player_added(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(gate):
			gate._add_peer(peer_id)


## Notifies all registered gates that a player left the scene.
## Called by [NetwScene] from [code]_on_despawned[/code].
func _notify_gates_player_removed(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(gate):
			gate._remove_peer(peer_id)
	_cleanup_dead_gates()


func _cleanup_dead_gates() -> void:
	_readiness_gates = _readiness_gates.filter(
		func(wr: WeakRef) -> bool: return is_instance_valid(wr.get_ref())
	)


func _get_peer_id(node: Node) -> int:
	var entity := NetwEntity.of(node)
	if entity and entity.peer_id != 0:
		return entity.peer_id
	return NetwEntity.parse_peer(node.name)


# ---------------------------------------------------------------------------
# RPCs - suspend / resume (soft, signal-only, game code decides what to do)
# ---------------------------------------------------------------------------

## Sent by the server to notify all clients that the scene has been suspended.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_suspend(reason: String) -> void:
	get_context().scene.suspended.emit(reason)


## Sent by the server to notify all clients that the scene has been resumed.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_resume() -> void:
	get_context().scene.resumed.emit()


## Sent by a client to ask the server to suspend the scene.
## The server emits [signal NetwScene.suspend_requested]; game code decides
## whether to honour the request by calling [method NetwScene.suspend].
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_suspend(reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn("_rpc_request_suspend received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var peer_id := multiplayer.get_remote_sender_id()
	get_context().scene.suspend_requested.emit(peer_id, reason)


# ---------------------------------------------------------------------------
# RPCs - countdown
# ---------------------------------------------------------------------------

## Sent by the server when a new countdown starts.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_started(seconds: int) -> void:
	get_context().scene.countdown_started.emit(seconds)


## Sent by the server on each countdown tick.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_tick(seconds_left: int) -> void:
	get_context().scene.countdown_tick.emit(seconds_left)


## Sent by the server when the countdown reaches zero.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_finished() -> void:
	get_context().scene.countdown_finished.emit()


## Sent by the server when a running countdown is cancelled.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_cancelled() -> void:
	get_context().scene.countdown_cancelled.emit()


# ---------------------------------------------------------------------------
# RPCs - readiness
# ---------------------------------------------------------------------------

## Sent by a client to report their ready state to the server.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_set_ready(is_ready: bool) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn("_rpc_request_set_ready received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var peer_id := multiplayer.get_remote_sender_id()
	_handle_set_ready(peer_id, is_ready)


## Broadcast by the server to synchronise a readiness change on scene peers.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_ready_changed(peer_id: int, is_ready: bool) -> void:
	for wr: WeakRef in _readiness_gates:
		var gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(gate):
			gate._receive_ready_changed(peer_id, is_ready)
	_cleanup_dead_gates()
