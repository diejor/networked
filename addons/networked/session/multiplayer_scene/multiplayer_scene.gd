class_name MultiplayerScene
extends Node
## Container for one replicated level scene.
##
## [member level], [member gate], and [member layer] define one admission
## boundary. Clients receive the subtree only after [method connect_peer] or
## [method register_player] admits their peer.
## [codeblock]
## var player := MultiplayerEntity.instantiate_player(participant)
## scene.add_player(player)
##
## scene.prepare_player_reparent(player)
## player.reparent(scene.level)
## scene.complete_player_reparent(player)
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

## Emitted when the server suspends this scene.
signal suspended(reason: String)
## Emitted when the server resumes this scene.
signal resumed()
## Emitted when a client asks the server to suspend this scene.
signal suspend_requested(peer_id: int, reason: String)
## Emitted when the server starts a countdown.
signal countdown_started(seconds: int)
## Emitted on each countdown tick.
signal countdown_tick(seconds_left: int)
## Emitted when the countdown reaches zero.
signal countdown_finished()
## Emitted when the countdown is cancelled.
signal countdown_cancelled()

## Emitted when a peer becomes admitted to this scene.
signal peer_admitted(peer_id: int)

## Emitted when a peer is released from this scene.
signal peer_released(peer_id: int)

# Active readiness gates registered via NetwScene.
var _readiness_gates: Array[WeakRef] = []
# Held strongly while the countdown is running so the timer stays alive.
var _active_countdown: NetwScene.Countdown
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


func _ready() -> void:
	if not _is_server():
		get_context()


func _exit_tree() -> void:
	_clear_participant_scene_membership()
	if _active_countdown:
		_active_countdown.cancel()
		_active_countdown = null
	if _context and _context.has_scene():
		_context.scene.close()


func _clear_participant_scene_membership() -> void:
	var ctx := get_context()
	if ctx == null or not ctx.has_scene():
		return
	var mt := MultiplayerTree.for_node(self)
	if mt and mt.local_participant:
		_clear_participant_current_scene(mt.local_participant)
	for participant: NetwParticipant in participants:
		_clear_participant_current_scene(participant)


func _clear_participant_current_scene(participant: NetwParticipant) -> void:
	var current := participant.current_scene
	if current and current.unwrap() == self:
		participant.current_scene = null


func _notify_participant_scene_released(participant: NetwParticipant) -> void:
	var current := participant.current_scene
	if current == null or current.unwrap() != self:
		return
	var mt := MultiplayerTree.for_node(self)
	if mt:
		mt._notify_local_scene_released(
			participant.peer_id,
			scene_layer_id(),
		)


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

## Participants admitted to this scene.
var participants: Array[NetwParticipant]:
	get:
		var out: Array[NetwParticipant] = []
		var mt := MultiplayerTree.for_node(self)
		if mt == null:
			return out
		for peer_id: int in connected_peers:
			var participant := mt.get_participant(peer_id)
			if participant:
				out.append(participant)
		return out

## Locally tracked player and entity [Node]s for this scene.
var tracked_nodes: Dictionary[Node, bool]:
	get:
		return _tracked_nodes


## Returns currently tracked player and entity [NetwEntity]s.
func player_nodes() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	for node: Node in _tracked_nodes:
		if is_instance_valid(node):
			var entity := NetwEntity.of(node)
			if entity != null:
				out.append(entity)
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
	var was_connected := connected_peers.has(peer_id)
	l.add_viewer(peer_id)
	if not was_connected and connected_peers.has(peer_id):
		peer_admitted.emit(peer_id)


## Removes [param peer_id] from this scene.
func disconnect_peer(peer_id: int) -> void:
	var l := layer
	if l == null:
		return
	var was_connected := connected_peers.has(peer_id)
	l.remove_viewer(peer_id)
	if was_connected and not connected_peers.has(peer_id):
		peer_released.emit(peer_id)


## Admits [param participant] to this scene.
##
## This is the participant-facing equivalent of [method connect_peer].
## [br][br][b]Server Only.[/b]
func admit(participant: NetwParticipant) -> void:
	if participant == null:
		return
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.admit() must be called on the server.",
	)
	connect_peer(participant.peer_id)
	_flush_gate_now()


## Releases [param participant] from this scene.
##
## This is the participant-facing equivalent of [method disconnect_peer].
## [br][br][b]Server Only.[/b]
func release(participant: NetwParticipant) -> void:
	if participant == null:
		return
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.release() must be called on the server.",
	)
	_notify_participant_scene_released(participant)
	disconnect_peer(participant.peer_id)
	_flush_gate_now()


## Moves [param moving_participants] into this scene.
##
## Returns a [NetwScene.MoveBatch] for observing arrivals.
## [br][br][b]Server Only.[/b]
func move_participants(
		moving_participants: Array[NetwParticipant],
) -> NetwScene.MoveBatch:
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.move_participants() must be called on the server.",
	)
	var ctx := get_context()
	if ctx == null or not ctx.has_scene():
		return null
	var batch := NetwScene.MoveBatch.new(moving_participants)
	for participant: NetwParticipant in moving_participants:
		if participant:
			participant.move_to(ctx.scene)
			batch._queue_arrival(participant)
	batch._flush.call_deferred()
	return batch


## Suspends until at least [param n] participants are admitted.
func wait_for_participants(n: int) -> void:
	while participants.size() < n:
		await peer_admitted


## Broadcasts a soft-suspend notification to scene peers.
##
## [br][br][b]Server Only.[/b]
func suspend(reason: String = "") -> void:
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.suspend() must be called on the server.",
	)
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_suspend.rpc_id(peer_id, reason)
	suspended.emit(reason)


## Asks the server to suspend the scene.
##
## [br][br][b]Player request.[/b]
func request_suspend(reason: String = "") -> void:
	_rpc_request_suspend.rpc_id(1, reason)


## Broadcasts a resume notification to scene peers.
##
## [br][br][b]Server Only.[/b]
func resume() -> void:
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.resume() must be called on the server.",
	)
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_resume.rpc_id(peer_id)
	resumed.emit()


## Starts a server-driven countdown.
##
## [br][br][b]Server Only.[/b]
func start_countdown(
		seconds: int,
		tick_interval: float = 1.0,
) -> NetwScene.Countdown:
	assert(
		seconds > 0,
		"MultiplayerScene.start_countdown(): seconds must be > 0.",
	)
	assert(
		tick_interval > 0.0,
		"MultiplayerScene.start_countdown(): tick_interval must be > 0.",
	)
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.start_countdown() must be called on the server.",
	)
	cancel_countdown()
	var cd := NetwScene.Countdown.new(self, seconds, tick_interval)
	_active_countdown = cd
	cd.tick.connect(_on_countdown_tick)
	cd.finished.connect(_on_countdown_finished)
	cd.cancelled.connect(_on_countdown_cancelled)
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_countdown_started.rpc_id(peer_id, seconds)
	countdown_started.emit(seconds)
	cd._start()
	return cd


## Cancels the currently running countdown, if any.
##
## [br][br][b]Server Only.[/b]
func cancel_countdown() -> void:
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.cancel_countdown() must be called on the server.",
	)
	if _active_countdown and _active_countdown.is_running():
		_active_countdown.cancel()
	_active_countdown = null


## Creates a readiness gate for this scene.
func create_readiness_gate() -> NetwScene.Readiness:
	var readiness := NetwScene.Readiness.new(self)
	_register_readiness_gate(readiness)
	for participant: NetwParticipant in participants:
		readiness._add_peer(participant.peer_id)
	return readiness


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
## [method track_node] is for explicit scene enrollment and reparent flows.
## [codeblock]
## scene.track_node(projectile)
## scene.connect_peer(target_peer_id)
## [/codeblock]
func track_node(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if not _owns_entity_record(node, entity):
		_debug_report_missing_own_entity(node, entity)
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
	var multiplayer_entity := MultiplayerEntity.unwrap(node)
	if multiplayer_entity:
		multiplayer_entity._debug_validate_spawn_identity()
	if _owns_entity_record(node, entity):
		_tracked_nodes[node] = true
		if is_instance_valid(gate):
			gate.track_entity(entity)
	else:
		_debug_report_missing_own_entity(node, entity)
	if node.is_inside_tree():
		spawned.emit(node)
	else:
		node.tree_entered.connect(
			_emit_spawned_on_entered.bind(node),
			CONNECT_ONE_SHOT,
		)


func _emit_spawned_on_entered(node: Node) -> void:
	if is_instance_valid(node):
		spawned.emit(node)


func _on_despawned(node: Node) -> void:
	if not is_instance_valid(node):
		return
	var entity := NetwEntity.of(node)
	if _owns_entity_record(node, entity):
		if is_instance_valid(gate):
			gate.untrack_entity(entity)
	_tracked_nodes.erase(node)
	despawned.emit(node)


func _owns_entity_record(node: Node, entity: NetwEntity) -> bool:
	return entity != null and entity.owner == node


func _debug_report_missing_own_entity(
		node: Node,
		entity: NetwEntity,
) -> void:
	if not OS.is_debug_build():
		return
	if not is_instance_valid(gate):
		return
	var detail := "it resolved to no record"
	if entity != null and is_instance_valid(entity.owner):
		detail = "it resolved to ancestor '%s'" % entity.owner.name
	var msg := (
			"MultiplayerScene '%s': spawned node '%s' has no NetwEntity " +
			"of its own. %s. Under a gated scene every spawned entity " +
			"needs its own identity. Add MultiplayerEntity, or call " +
			"NetwEntity.ensure(node) and MultiplayerScene.track_node(node)."
	)
	Netw.dbg.error(
		msg,
		[name, node.name, detail],
		func(m): push_error(m),
	)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()

# Player enrollment.


## Registers [param player] and adds it to [member level].
## [codeblock]
## var player_node := spawner.instantiate_player(participant)
## scene.add_player(NetwEntity.of(player_node))
## [/codeblock]
func add_player(player: NetwEntity) -> void:
	track_node(player.owner)
	register_player(player)
	level.add_child(player.owner)
	player.owner.owner = level
	_flush_interest_now()


## Admits [param player]'s peer before [method Node.reparent].
##
## Call this before moving a player into [member level] so [member gate]
## visibility is flushed before spawn packets target the new scene.
## [codeblock]
## scene.prepare_player_reparent(NetwEntity.of(player))
## player.reparent(scene.level)
## scene.complete_player_reparent(NetwEntity.of(player))
## [/codeblock]
func prepare_player_reparent(player: NetwEntity) -> void:
	var peer_id := player.peer_id
	if peer_id == 0:
		Netw.dbg.error(
			"Cannot prepare player '%s': peer_id is 0.",
			[player.owner.name],
			func(m): push_error(m)
		)
		return
	_players_by_peer[peer_id] = weakref(player.owner)
	connect_peer(peer_id)
	_flush_gate_now()


## Completes a reparent after [param player] enters this scene.
##
## [method complete_player_reparent] calls [method register_player] and
## flushes interest before follow up RPCs target the player subtree.
func complete_player_reparent(player: NetwEntity) -> void:
	register_player(player)
	_flush_interest_now()


## Registers [param player] as this scene's player for its peer id.
##
## [method register_player] admits the peer, calls [method track_node], and
## indexes the player for [method get_players].
func register_player(player: NetwEntity) -> void:
	var peer_id := player.peer_id
	if peer_id == 0:
		Netw.dbg.error(
			"Cannot register player '%s': peer_id is 0.",
			[player.owner.name],
			func(m): push_error(m)
		)
		return
	var previous_peer := _find_peer_for_player(player.owner)
	if previous_peer != 0 and previous_peer != peer_id:
		_players_by_peer.erase(previous_peer)
		disconnect_peer(previous_peer)
	_players_by_peer[peer_id] = weakref(player.owner)
	connect_peer(peer_id)
	track_node(player.owner)
	_assign_local_player_if_needed(player, peer_id)
	_flush_gate_now()
	var bound := _on_player_exiting.bind(player.owner)
	if not player.owner.tree_exiting.is_connected(bound):
		player.owner.tree_exiting.connect(bound)


## Returns live player [NetwEntity]s registered in this scene.
##
## Players are indexed weakly. Freed players are pruned when this method runs.
func get_players() -> Array[NetwEntity]:
	var players: Array[NetwEntity] = []
	var stale_peers: Array[int] = []
	for peer_id: int in _players_by_peer:
		var player := _players_by_peer[peer_id].get_ref() as Node
		if is_instance_valid(player):
			var entity := NetwEntity.of(player)
			if entity != null:
				players.append(entity)
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


func _assign_local_player_if_needed(player: NetwEntity, peer_id: int) -> void:
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.multiplayer_api:
		return
	if peer_id != mt.multiplayer_api.get_unique_id():
		return
	if player.owner.is_node_ready():
		mt.local_player = player
		return
	if not player.owner.ready.is_connected(_assign_ready_local_player.bind(player)):
		player.owner.ready.connect(
			_assign_ready_local_player.bind(player),
			CONNECT_ONE_SHOT,
		)


func _assign_ready_local_player(player: NetwEntity) -> void:
	var mt := MultiplayerTree.resolve(self)
	if mt and player != null and is_instance_valid(player.owner):
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
func _register_readiness_gate(readiness_gate: NetwScene.Readiness) -> void:
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
		var readiness_gate := wr.get_ref() as NetwScene.Readiness
		if is_instance_valid(readiness_gate):
			readiness_gate._add_peer(peer_id)


# Notifies all registered gates that a player left the scene.
func _notify_gates_player_removed(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var readiness_gate := wr.get_ref() as NetwScene.Readiness
		if is_instance_valid(readiness_gate):
			readiness_gate._remove_peer(peer_id)
	_cleanup_dead_gates()


func _cleanup_dead_gates() -> void:
	_readiness_gates = _readiness_gates.filter(
		func(wr: WeakRef) -> bool: return is_instance_valid(wr.get_ref())
	)


func _on_countdown_tick(seconds_left: int) -> void:
	countdown_tick.emit(seconds_left)
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_countdown_tick.rpc_id(peer_id, seconds_left)


func _on_countdown_finished() -> void:
	countdown_finished.emit()
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_countdown_finished.rpc_id(peer_id)
	_active_countdown = null


func _on_countdown_cancelled() -> void:
	countdown_cancelled.emit()
	for peer_id: int in connected_peers:
		if peer_id == multiplayer.get_unique_id():
			continue
		_rpc_receive_countdown_cancelled.rpc_id(peer_id)
	_active_countdown = null


func _get_peer_id(node: Node) -> int:
	var entity := NetwEntity.of(node)
	if entity and entity.peer_id != 0:
		return entity.peer_id
	return NetwEntity.parse_peer(node.name)

# Suspend and resume RPC handlers.


# Sent by the server to notify all clients that the scene has been suspended.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_suspend(reason: String) -> void:
	suspended.emit(reason)


# Sent by the server to notify all clients that the scene has been resumed.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_resume() -> void:
	resumed.emit()


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
	suspend_requested.emit(peer_id, reason)

# Countdown RPC handlers.


# Sent by the server when a new countdown starts.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_started(seconds: int) -> void:
	countdown_started.emit(seconds)


# Sent by the server on each countdown tick.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_tick(seconds_left: int) -> void:
	countdown_tick.emit(seconds_left)


# Sent by the server when the countdown reaches zero.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_finished() -> void:
	countdown_finished.emit()


# Sent by the server when a running countdown is cancelled.
@rpc("authority", "call_local", "reliable")
func _rpc_receive_countdown_cancelled() -> void:
	countdown_cancelled.emit()

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
		var readiness_gate := wr.get_ref() as NetwScene.Readiness
		if is_instance_valid(readiness_gate):
			readiness_gate._receive_ready_changed(peer_id, is_ready)
	_cleanup_dead_gates()
