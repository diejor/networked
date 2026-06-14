class_name MultiplayerScene
extends Node
## Container for one replicated level scene.
##
## [member level], [member gate], and [member layer] define one admission
## boundary. Clients receive the subtree only after [method connect_peer] or
## [method register_player] admits their peer.
## [codeblock]
## var player := MultiplayerEntity.instantiate_player(rj)
## scene.add_player(player)
##
## scene.prepare_player_transfer(player)
## player.reparent(scene.level)
## scene.complete_player_transfer(player)
## [/codeblock]

## [InterestGate] carrying admission state for [member layer].
@export var gate: InterestGate

## Instantiated level root for this scene.
##
## Assignment adds the level as a child, names this scene, binds
## [member InterestGate.layer_id], and calls [method hook_spawn_signals].
var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		if is_instance_valid(gate):
			gate.layer_id = scene_layer_id()
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self
		if is_instance_valid(gate):
			NetwEntity.ensure(self).provide(
				NetwEntity.Slot.INTEREST_GATE,
				gate,
			)

var _context: NetwContext

## Emitted when a tracked [Node] enters this scene.
signal spawned(node: Node)

## Emitted when a tracked [Node] exits this scene.
signal despawned(node: Node)

## Legacy alias for [signal spawned].
signal player_spawned(node: Node)

## Legacy alias for [signal despawned].
signal player_despawned(node: Node)

## Emitted when [NetwSceneReadiness] marks a player ready.
##
## This is a manual readiness signal. Use [signal spawned] for scene entry.
signal player_ready(rj: ResolvedJoin)

# Active readiness gates registered via NetwScene.
var _readiness_gates: Array[WeakRef] = []
# Players indexed by peer. Weak refs keep the scene from owning players.
var _players_by_peer: Dictionary[int, WeakRef] = { }
var _tracked_nodes: Dictionary[Node, bool] = { }


## Stable [NetwInterestLayer] id for [member level].
## [codeblock]
## &"scene:Arena"
## [/codeblock]
func scene_layer_id() -> StringName:
	if not is_instance_valid(level):
		return &""
	return StringName("scene:%s" % level.name)

## Returns the [NetwInterestLayer] for [method scene_layer_id].
var layer: NetwInterestLayer:
	get:
		var ctx := get_context()
		if not ctx or ctx.interest == null:
			return null
		var id := scene_layer_id()
		if id.is_empty():
			return null
		return ctx.interest.layer(id)


## Returns the [NetwContext] for this scene.
func get_context() -> NetwContext:
	if not _context or not _context.is_valid():
		var mt := MultiplayerTree.for_node(self)
		if not mt:
			Netw.dbg.error(
				"Scene.get_context(): MultiplayerTree not found.",
				func(m): push_error(m)
			)
			return null
		var scene_ctx := NetwScene.new(self)
		_context = NetwContext.new(mt, scene_ctx)
	return _context


func _on_tree_exiting() -> void:
	if _context and _context.has_scene():
		_context.scene.close()


## Connects [param level]'s [MultiplayerSpawner]s to scene tracking.
func hook_spawn_signals(level: Node) -> void:
	var spawners := get_spawners(level)
	for spawner in spawners:
		if not spawner.spawned.is_connected(_on_spawned):
			spawner.spawned.connect(_on_spawned)
		if not spawner.despawned.is_connected(_on_despawned):
			spawner.despawned.connect(_on_despawned)

## Peer ids admitted to [member gate].
var connected_peers: Dictionary[int, bool]:
	get:
		var l := layer
		if l == null:
			return { }
		return l.viewers

## Locally tracked player and entity [Node]s for this scene.
var tracked_nodes: Dictionary[Node, bool]:
	get:
		return _tracked_nodes


## Returns currently tracked player and entity [Node]s.
func player_nodes() -> Array[Node]:
	var out: Array[Node] = []
	out.assign(_tracked_nodes.keys())
	return out


## Returns all [MultiplayerSpawner]s under [param node].
func get_spawners(node: Node) -> Array[MultiplayerSpawner]:
	var spawners: Array[MultiplayerSpawner] = []
	spawners.assign(node.find_children("*", "MultiplayerSpawner"))
	return spawners

# Admission API.


## Admits [param peer_id] to this scene.
##
## [method connect_peer] routes through [method NetwInterestLayer.add_viewer].
## Peer id [code]0[/code] is invalid.
func connect_peer(peer_id: int) -> void:
	if peer_id == 0:
		Netw.dbg.error(
			"MultiplayerScene.connect_peer(0) is invalid.",
			[],
			func(m): push_error(m)
		)
		return
	var l := layer
	if l == null:
		return
	l.add_viewer(peer_id)


## Removes [param peer_id] from this scene.
func disconnect_peer(peer_id: int) -> void:
	var l := layer
	if l == null:
		return
	l.remove_viewer(peer_id)


## Returns the admission verdict for [param peer_id].
##
## The server reads [member layer]. Clients read the replicated
## [member gate] mirror.
func scene_visibility_filter(peer_id: int) -> bool:
	var l := layer
	if l == null:
		return false
	return l.verdict_for(peer_id)

# Entity tracking.


## Enrolls [param node]'s [NetwEntity] in [member layer].
##
## [method track_node] is for explicit scene enrollment and transfer flows.
## [codeblock]
## scene.track_node(projectile)
## scene.connect_peer(target_peer_id)
## [/codeblock]
func track_node(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity == null:
		Netw.dbg.warn(
			"MultiplayerScene.track_node: %s has no NetwEntity",
			[node.name],
		)
		return
	_tracked_nodes[node] = true
	if is_instance_valid(gate):
		gate.track_entity(entity)
	var on_spawned := _on_spawned.bind(node)
	if not node.tree_entered.is_connected(on_spawned):
		node.tree_entered.connect(on_spawned)
	var on_despawned := _on_despawned.bind(node)
	if not node.tree_exiting.is_connected(on_despawned):
		node.tree_exiting.connect(on_despawned)


## Reverses [method track_node].
func untrack_node(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var peer_id := _get_peer_id(node)
	if not node.is_inside_tree():
		var entity := NetwEntity.of(node)
		if entity and is_instance_valid(gate):
			gate.untrack_entity(entity)
	_tracked_nodes.erase(node)
	var on_spawned := _on_spawned.bind(node)
	if node.tree_entered.is_connected(on_spawned):
		node.tree_entered.disconnect(on_spawned)
	var on_despawned := _on_despawned.bind(node)
	if node.tree_exiting.is_connected(on_despawned):
		node.tree_exiting.disconnect(on_despawned)
	if peer_id != 0:
		_notify_gates_player_removed(peer_id)

# Spawner event dispatch.


func _on_spawned(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity != null:
		_tracked_nodes[node] = true
		if is_instance_valid(gate):
			gate.track_entity(entity)
	if node.is_inside_tree():
		spawned.emit(node)
		player_spawned.emit(node)
	else:
		node.tree_entered.connect(
			_emit_spawned_on_entered.bind(node),
			CONNECT_ONE_SHOT,
		)


func _emit_spawned_on_entered(node: Node) -> void:
	if is_instance_valid(node):
		spawned.emit(node)
		player_spawned.emit(node)


func _on_despawned(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if entity:
		if is_instance_valid(gate):
			gate.untrack_entity(entity)
	_tracked_nodes.erase(node)
	despawned.emit(node)
	player_despawned.emit(node)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()

# Player enrollment.


## Registers [param player] and adds it to [member level].
## [codeblock]
## var player := spawner.instantiate_player(rj)
## scene.add_player(player)
## [/codeblock]
func add_player(player: Node) -> void:
	track_node(player)
	register_player(player)
	level.add_child(player)
	player.owner = level
	_flush_interest_now()


## Admits [param player]'s peer before [method Node.reparent].
##
## Call this before moving a player into [member level] so [member gate]
## visibility is flushed before spawn packets target the new scene.
## [codeblock]
## scene.prepare_player_transfer(player)
## player.reparent(scene.level)
## scene.complete_player_transfer(player)
## [/codeblock]
func prepare_player_transfer(player: Node) -> void:
	var peer_id := _get_peer_id(player)
	if peer_id == 0:
		Netw.dbg.error(
			"Cannot prepare player '%s': peer_id is 0.",
			[player.name],
			func(m): push_error(m)
		)
		return
	_players_by_peer[peer_id] = weakref(player)
	connect_peer(peer_id)
	_flush_gate_now()


## Completes a transfer after [param player] enters this scene.
##
## [method complete_player_transfer] calls [method register_player] and
## flushes interest before follow up RPCs target the player subtree.
func complete_player_transfer(player: Node) -> void:
	register_player(player)
	_flush_interest_now()


## Registers [param player] as this scene's player for its peer id.
##
## [method register_player] admits the peer, calls [method track_node], and
## indexes the player for [method get_players].
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
		disconnect_peer(previous_peer)
	_players_by_peer[peer_id] = weakref(player)
	connect_peer(peer_id)
	track_node(player)
	_assign_local_player_if_needed(player, peer_id)
	_flush_gate_now()
	var bound := _on_player_exiting.bind(player)
	if not player.tree_exiting.is_connected(bound):
		player.tree_exiting.connect(bound)


## Returns live player [Node]s registered in this scene.
##
## Players are indexed weakly. Freed players are pruned when this method runs.
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
		disconnect_peer(peer_id)


func _find_peer_for_player(player: Node) -> int:
	for peer_id: int in _players_by_peer:
		if _players_by_peer[peer_id].get_ref() == player:
			return peer_id
	return 0


func _assign_local_player_if_needed(player: Node, peer_id: int) -> void:
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.multiplayer_api:
		return
	if peer_id != mt.multiplayer_api.get_unique_id():
		return
	if player.is_node_ready():
		mt.local_player = player
		return
	if not player.ready.is_connected(_assign_ready_local_player.bind(player)):
		player.ready.connect(
			_assign_ready_local_player.bind(player),
			CONNECT_ONE_SHOT,
		)


func _assign_ready_local_player(player: Node) -> void:
	var mt := MultiplayerTree.resolve(self)
	if mt and is_instance_valid(player):
		mt.local_player = player


func _flush_interest_now() -> void:
	if not _is_server():
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return
	var service := mt.get_service(InterestService) as InterestService
	if service:
		service.flush()


func _flush_gate_now() -> void:
	if not _is_server():
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return
	var service := mt.get_service(InterestService) as InterestService
	if service:
		service.flush_gates()

# Readiness gate helpers.


# Registers a readiness gate to receive peer updates.
func _register_readiness_gate(readiness_gate: NetwSceneReadiness) -> void:
	_cleanup_dead_gates()
	_readiness_gates.append(weakref(readiness_gate))


# Applies a readiness change from the server and broadcasts to scene peers.
func _handle_set_ready(peer_id: int, is_ready: bool) -> void:
	_rpc_receive_ready_changed(peer_id, is_ready)
	for target_peer_id: int in connected_peers:
		if target_peer_id != multiplayer.get_unique_id():
			rpc_id(target_peer_id, "_rpc_receive_ready_changed", peer_id, is_ready)


# Notifies all registered gates that a player entered the scene.
func _notify_gates_player_added(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var readiness_gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(readiness_gate):
			readiness_gate._add_peer(peer_id)


# Notifies all registered gates that a player left the scene.
func _notify_gates_player_removed(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var readiness_gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(readiness_gate):
			readiness_gate._remove_peer(peer_id)
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

# Suspend and resume RPC handlers.


# Sent by the server to notify all clients that the scene has been suspended.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_suspend(reason: String) -> void:
	get_context().scene.suspended.emit(reason)


# Sent by the server to notify all clients that the scene has been resumed.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_resume() -> void:
	get_context().scene.resumed.emit()


# Handles a client suspend request.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_suspend(reason: String) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"_rpc_request_suspend received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()
	get_context().scene.suspend_requested.emit(peer_id, reason)

# Countdown RPC handlers.


# Sent by the server when a new countdown starts.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_started(seconds: int) -> void:
	get_context().scene.countdown_started.emit(seconds)


# Sent by the server on each countdown tick.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_tick(seconds_left: int) -> void:
	get_context().scene.countdown_tick.emit(seconds_left)


# Sent by the server when the countdown reaches zero.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_finished() -> void:
	get_context().scene.countdown_finished.emit()


# Sent by the server when a running countdown is cancelled.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_cancelled() -> void:
	get_context().scene.countdown_cancelled.emit()

# Readiness RPC handlers.


# Sent by a client to report their ready state to the server.
@rpc("any_peer", "call_local", "reliable")
func _rpc_request_set_ready(is_ready: bool) -> void:
	if not multiplayer.is_server():
		Netw.dbg.warn(
			"_rpc_request_set_ready received on non-server peer %d",
			[multiplayer.get_unique_id()],
		)
		return
	var peer_id := multiplayer.get_remote_sender_id()
	_handle_set_ready(peer_id, is_ready)


# Broadcast by the server to synchronise a readiness change on scene peers.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_ready_changed(peer_id: int, is_ready: bool) -> void:
	for wr: WeakRef in _readiness_gates:
		var readiness_gate := wr.get_ref() as NetwSceneReadiness
		if is_instance_valid(readiness_gate):
			readiness_gate._receive_ready_changed(peer_id, is_ready)
	_cleanup_dead_gates()
