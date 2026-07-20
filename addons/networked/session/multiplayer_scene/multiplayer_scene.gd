class_name MultiplayerScene
extends Node
## Container for one replicated level scene and its admission boundary.
##
## [member level] and [member layer] define one admission boundary. The wrapper
## is an ordinary entity in that layer, and every descendant entity inherits
## its committed row through the interest engine's parent clamp. A client
## receives the subtree only after [method connect_peer] or
## [method register_player] admits its peer. A participant is admitted to
## exactly one scene at a time. The container's runtime root is chosen by
## [member NetwSceneConfig.concurrency] because two scenes active at once in one
## [SceneTree] would otherwise share a physics world and collide. Under
## [constant NetwSceneConfig.Concurrency.CONCURRENT] a hosting peer carries this
## script on a world-owning [SubViewport]. Under
## [constant NetwSceneConfig.Concurrency.SINGLE], and on every client, the root
## is a plain [Node] because only one scene is ever mounted.
## [codeblock]
## var node := template_entity.instantiate_player(participant)
## var player := NetwEntity.of(node)
## scene.add_player(player)
##
## scene.prepare_player_reparent(player)
## node.reparent(scene.level)
## scene.complete_player_reparent(player)
## [/codeblock]

## Instantiated level root for this scene.
##
## Assignment adds the level as a child, names this scene, and calls
## [method hook_spawn_signals].
var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self

## Emitted when a tracked [Node] enters this scene.
signal spawned(node: Node)

## Emitted when a tracked [Node] exits this scene.
signal despawned(node: Node)

## Emitted when a player identity is spawned into this scene.
signal player_entered(player: NetwEntity)

## Emitted when a player identity is despawned from this scene.
signal player_left(player: NetwEntity)

## Emitted when a participant is admitted to this scene.
signal participant_entered(participant: NetwParticipant)

## Emitted when a participant is released from this scene.
signal participant_left(participant: NetwParticipant)

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

# Active readiness gates registered via create_readiness_gate.
var _readiness_gates: Array[WeakRef] = []
# Held strongly while the countdown is running so the timer stays alive.
var _active_countdown: Countdown
# Players indexed by peer. Weak refs keep the scene from owning players.
var _players_by_peer: Dictionary[int, WeakRef] = { }
var _tracked_nodes: Dictionary[Node, bool] = { }
# Client-side admission mirror the participant-fact handlers subscribe to.
var _admission_layer: NetwInterestLayer
# Peers admitted before their participant existed, retried on join.
var _pending_admitted_peers: Dictionary[int, bool] = { }


## Returns the [MultiplayerScene] containing [param node], or [code]null[/code].
static func of(node: Node) -> MultiplayerScene:
	if not is_instance_valid(node):
		return null
	var api := NetwMultiplayer.of(node)
	if api:
		return api.scenes.scene_of(node)
	var current := node
	while current:
		if current is MultiplayerScene:
			return current as MultiplayerScene
		current = current.get_parent()
	return null


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
		var api := NetwMultiplayer.of(self)
		if not api:
			return null
		var id := scene_layer_id()
		if id.is_empty():
			return null
		return api.interest.layer(id)

## The level scene root name for this scene.
##
## Returns [code]&""[/code] until [member level] is assigned.
var scene_name: StringName:
	get:
		if not is_instance_valid(level):
			return &""
		return StringName(level.name)

## All player identities currently in this scene.
##
## Mirrors [method get_players] as a read-only property.
var players: Array[NetwEntity]:
	get:
		return get_players()

## The number of players currently in this scene.
var player_count: int:
	get:
		return get_players().size()

## The player identity owned by the local peer, or [code]null[/code].
var local_player: NetwEntity:
	get:
		if not is_inside_tree() or multiplayer == null:
			return null
		var local_id := multiplayer.get_unique_id()
		for player: NetwEntity in get_players():
			if player.peer_id == local_id:
				return player
		return null

## The peer IDs currently admitted to this scene.
##
## Use this to enumerate peers when sending custom broadcast RPCs:
## [codeblock]
## for peer_id in scene.peers:
##     _rpc_notify.rpc_id(peer_id, message)
## [/codeblock]
var peers: Array[int]:
	get:
		var result: Array[int] = []
		result.assign(connected_peers.keys())
		return result


func _ready() -> void:
	_bind_admission_bus()
	_bind_participant_facts()


func _exit_tree() -> void:
	_unbind_admission_bus()
	_unbind_participant_facts()
	_clear_participant_scene_membership()
	if _active_countdown:
		_active_countdown.cancel()
		_active_countdown = null


func _clear_participant_scene_membership() -> void:
	if not is_instance_valid(level):
		return
	var api := NetwMultiplayer.of(self)
	if api and api.local_participant:
		_clear_participant_current_scene(api.local_participant)
	for participant: NetwParticipant in participants:
		_clear_participant_current_scene(participant)


func _clear_participant_current_scene(participant: NetwParticipant) -> void:
	if participant.current_scene == self:
		participant.current_scene = null


func _notify_participant_scene_released(participant: NetwParticipant) -> void:
	if participant.current_scene != self:
		return
	var api := NetwMultiplayer.of(self)
	if api == null:
		return
	var payload := var_to_bytes(scene_layer_id())
	if participant.peer_id == api.get_unique_id():
		api.scenes._handle_scene_released_frame(payload, 1)
	else:
		api.replication.send_to(
			participant.peer_id,
			0,
			NetwFrameEnvelope.Channel.SESSION_SCENE_RELEASED,
			payload,
			true,
		)


## Connects [param level]'s [MultiplayerSpawner]s to scene tracking.
func hook_spawn_signals(level: Node) -> void:
	var spawners := get_spawners(level)
	for spawner in spawners:
		if not spawner.spawned.is_connected(_on_spawned):
			spawner.spawned.connect(_on_spawned)
		if not spawner.despawned.is_connected(_on_despawned):
			spawner.despawned.connect(_on_despawned)


# Subscribes to the liveness bus so a routed entity enrolls in this scene the
# moment its route goes live, on every peer. The explicit track_node/add_player
# and spawner-signal paths stay for the offline and no-route entities that never
# reach the bus.
func _bind_admission_bus() -> void:
	var api := NetwMultiplayer.of(self)
	if not api:
		return
	if not api.liveness.entity_live.is_connected(_on_liveness_entity_live):
		api.liveness.entity_live.connect(_on_liveness_entity_live)


func _unbind_admission_bus() -> void:
	var api := NetwMultiplayer.of(self)
	if not api:
		return
	if api.liveness.entity_live.is_connected(_on_liveness_entity_live):
		api.liveness.entity_live.disconnect(_on_liveness_entity_live)


# Enrolls a newly live entity whose enclosing scene is this one. Idempotent with
# the explicit enrollment paths: a node already tracked is left alone.
func _on_liveness_entity_live(_route: int, entity: NetwEntity) -> void:
	if not is_instance_valid(entity) or not is_instance_valid(entity.owner):
		return
	if MultiplayerScene.of(entity.owner) != self:
		return
	if entity.peer_id != 0:
		if _players_by_peer.has(entity.peer_id) \
				and _players_by_peer[entity.peer_id].get_ref() == entity.owner:
			return
		register_player(entity)
	elif not _tracked_nodes.has(entity.owner):
		track_node(entity.owner)


# Wires the player and participant convenience signals. The server reads its
# own admission signals. A client reads awareness for the scene wrapper.
func _bind_participant_facts() -> void:
	if not spawned.is_connected(_on_player_spawned):
		spawned.connect(_on_player_spawned)
	if not despawned.is_connected(_on_player_despawned):
		despawned.connect(_on_player_despawned)
	if multiplayer and multiplayer.is_server():
		if not peer_admitted.is_connected(_on_participant_admitted):
			peer_admitted.connect(_on_participant_admitted)
		if not peer_released.is_connected(_on_participant_released):
			peer_released.connect(_on_participant_released)
		for peer_id: int in connected_peers:
			_on_participant_admitted(peer_id)
	else:
		_bind_client_admission_layer()


func _unbind_participant_facts() -> void:
	if spawned.is_connected(_on_player_spawned):
		spawned.disconnect(_on_player_spawned)
	if despawned.is_connected(_on_player_despawned):
		despawned.disconnect(_on_player_despawned)
	if peer_admitted.is_connected(_on_participant_admitted):
		peer_admitted.disconnect(_on_participant_admitted)
	if peer_released.is_connected(_on_participant_released):
		peer_released.disconnect(_on_participant_released)
	if _admission_layer:
		if _admission_layer.entity_visible.is_connected(
			_on_scene_entity_visible,
		):
			_admission_layer.entity_visible.disconnect(
				_on_scene_entity_visible,
			)
		if _admission_layer.entity_hidden.is_connected(
			_on_scene_entity_hidden,
		):
			_admission_layer.entity_hidden.disconnect(_on_scene_entity_hidden)
	var api := NetwMultiplayer.of(self)
	if api and api.participant_joined.is_connected(_on_api_participant_joined):
		api.participant_joined.disconnect(_on_api_participant_joined)


# Clients learn admission from route awareness for the wrapper entity.
func _bind_client_admission_layer() -> void:
	var api := NetwMultiplayer.of(self)
	if api == null:
		return
	if not api.participant_joined.is_connected(_on_api_participant_joined):
		api.participant_joined.connect(_on_api_participant_joined)
	var l := layer
	if l == null:
		return
	_admission_layer = l
	if not l.entity_visible.is_connected(_on_scene_entity_visible):
		l.entity_visible.connect(_on_scene_entity_visible)
	if not l.entity_hidden.is_connected(_on_scene_entity_hidden):
		l.entity_hidden.connect(_on_scene_entity_hidden)
	var scene_entity := NetwEntity.of(self)
	if scene_entity and l.has_entity(scene_entity):
		_admit_local_participant()


func _on_scene_entity_visible(entity: NetwEntity) -> void:
	if entity == NetwEntity.of(self):
		_admit_local_participant()


func _on_scene_entity_hidden(entity: NetwEntity) -> void:
	if entity == NetwEntity.of(self):
		_release_local_participant()


func _admit_local_participant() -> void:
	var api := NetwMultiplayer.of(self)
	if api and api.local_participant:
		_on_participant_admitted(api.local_participant.peer_id)


func _release_local_participant() -> void:
	var api := NetwMultiplayer.of(self)
	if api and api.local_participant:
		_on_participant_released(api.local_participant.peer_id)


func _on_participant_admitted(peer_id: int) -> void:
	var api := NetwMultiplayer.of(self)
	if api == null:
		return
	var participant := api.participant(peer_id)
	if participant == null:
		_pending_admitted_peers[peer_id] = true
		return
	_pending_admitted_peers.erase(peer_id)
	participant.current_scene = self
	participant_entered.emit(participant)
	_notify_gates_player_added(peer_id)


func _on_participant_released(peer_id: int) -> void:
	var api := NetwMultiplayer.of(self)
	if api == null:
		return
	var participant := api.participant(peer_id)
	if participant == null:
		return
	if participant.current_scene == self:
		_clear_current_scene_if_still_current.call_deferred(
			participant,
			get_instance_id(),
		)
	participant_left.emit(participant)
	_notify_gates_player_removed(peer_id)


# A participant may be admitted before its roster row lands. Retries the
# admission fact once the participant appears.
func _on_api_participant_joined(participant: NetwParticipant) -> void:
	if _admission_layer == null:
		return
	if participant != NetwMultiplayer.of(self).local_participant:
		return
	var scene_entity := NetwEntity.of(self)
	if not scene_entity or not _admission_layer.has_entity(scene_entity):
		return
	_on_participant_admitted(participant.peer_id)


# Clears membership only if this scene is still the participant's current one,
# so a move that reassigns current_scene before the deferred call wins.
func _clear_current_scene_if_still_current(
		participant: NetwParticipant,
		scene_instance_id: int,
) -> void:
	var current := participant.current_scene
	if is_instance_valid(current) \
			and current.get_instance_id() == scene_instance_id:
		participant.current_scene = null


func _on_player_spawned(node: Node) -> void:
	var entity := NetwEntity.of(node)
	if entity != null and entity in get_players():
		player_entered.emit(entity)


func _on_player_despawned(node: Node) -> void:
	if _get_peer_id(node) == 0:
		return
	player_left.emit(NetwEntity.of(node))

## Peer ids admitted through [member layer].
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
		var api := NetwMultiplayer.of(self)
		if api == null:
			return out
		for peer_id: int in connected_peers:
			var participant := api.get_participant(peer_id)
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
	_flush_interest_now()


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
	_flush_interest_now()


## Moves [param moving_participants] into this scene.
##
## Returns a [MultiplayerScene.MoveBatch] for observing arrivals.
## [br][br][b]Server Only.[/b]
func move_participants(
		moving_participants: Array[NetwParticipant],
) -> MoveBatch:
	assert(
		multiplayer.is_server(),
		"MultiplayerScene.move_participants() must be called on the server.",
	)
	var batch := MoveBatch.new(moving_participants)
	for participant: NetwParticipant in moving_participants:
		if participant:
			participant.move_to(self)
			batch._queue_arrival(participant)
	batch._flush.call_deferred()
	return batch


## Suspends until at least [param n] participants are admitted.
func wait_for_participants(n: int) -> void:
	while participants.size() < n:
		await peer_admitted


## Suspends until at least [param n] players are present.
func wait_for_players(n: int) -> void:
	while get_players().size() < n:
		await player_entered


## Returns the player identity owned by [param peer_id], or [code]null[/code].
func get_player_by_peer_id(peer_id: int) -> NetwEntity:
	for player: NetwEntity in get_players():
		if player.peer_id == peer_id:
			return player
	return null


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
) -> Countdown:
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
	var cd := Countdown.new(self, seconds, tick_interval)
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
func create_readiness_gate() -> Readiness:
	var readiness := Readiness.new(self)
	_register_readiness_gate(readiness)
	for participant: NetwParticipant in participants:
		readiness._add_peer(participant.peer_id)
	return readiness


## Returns the admission verdict for [param peer_id].
##
## Server authority reads [member layer].
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
	if _is_server():
		var scene_layer := layer
		if scene_layer:
			scene_layer.add_entity(entity)
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
	var entity := NetwEntity.of(node)
	if entity and _is_server():
		var scene_layer := layer
		if scene_layer:
			scene_layer.remove_entity(entity)
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
	if _owns_entity_record(node, entity):
		_tracked_nodes[node] = true
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
	var detail := "it resolved to no record"
	if entity != null and is_instance_valid(entity.owner):
		detail = "it resolved to ancestor '%s'" % entity.owner.name
	var msg := (
			"MultiplayerScene '%s': spawned node '%s' has no NetwEntity " +
			"of its own. %s. Every scene-spawned entity " +
			"needs its own identity. Call NetwEntity.ensure(node) before " +
			"MultiplayerScene.track_node(node)."
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
## Call this before moving a player into [member level] so the destination
## wrapper row is flushed before spawn packets target the new scene.
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
	_flush_interest_now()


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
	_flush_interest_now()
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


func _flush_interest_now() -> void:
	if not _is_server():
		return
	var api := NetwMultiplayer.of(self)
	if not api:
		return
	api.interest.flush_now()

# Readiness gate helpers.


# Registers a readiness gate to receive peer updates.
func _register_readiness_gate(readiness_gate: Readiness) -> void:
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
		var readiness_gate := wr.get_ref() as Readiness
		if is_instance_valid(readiness_gate):
			readiness_gate._add_peer(peer_id)


# Notifies all registered gates that a player left the scene.
func _notify_gates_player_removed(peer_id: int) -> void:
	for wr: WeakRef in _readiness_gates:
		var readiness_gate := wr.get_ref() as Readiness
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
		var readiness_gate := wr.get_ref() as Readiness
		if is_instance_valid(readiness_gate):
			readiness_gate._receive_ready_changed(peer_id, is_ready)
	_cleanup_dead_gates()


## Server-driven countdown that ticks once per second.
##
## Obtain via [method MultiplayerScene.start_countdown] - do not construct
## directly. Clients do not receive a return value; they listen to
## [signal MultiplayerScene.countdown_started] and the subsequent
## [signal MultiplayerScene.countdown_tick] /
## [signal MultiplayerScene.countdown_finished] signals, which are broadcast
## automatically.
## [codeblock]
## # Server:
## var cd := scene.start_countdown(10)
## await cd.finished
## start_match()
##
## # Client (connect before the server starts the countdown):
## scene.countdown_started.connect(func(n): $Timer.text = str(n))
## scene.countdown_tick.connect(func(n): $Timer.text = str(n))
## scene.countdown_finished.connect(start_match)
## [/codeblock]
class Countdown:
	extends RefCounted

	## Emitted each second with the remaining seconds (including 0 at the very end).
	signal tick(seconds_left: int)
	## Emitted when the countdown reaches zero.
	signal finished()
	## Emitted when [method cancel] is called before the countdown reaches zero.
	signal cancelled()

	var _scene_ref: WeakRef
	var _seconds_left: int
	var _tick_interval: float
	var _running: bool = false


	func _init(
			scene: MultiplayerScene,
			seconds: int,
			tick_interval: float = 1.0,
	) -> void:
		_scene_ref = weakref(scene)
		_seconds_left = seconds
		_tick_interval = tick_interval


	## Returns [code]true[/code] if the countdown is actively ticking.
	func is_running() -> bool:
		return _running

	## The number of seconds remaining.
	var seconds_left: int:
		get:
			return _seconds_left


	## Cancels the countdown and emits [signal cancelled].
	## Does nothing if the countdown is not running.
	func cancel() -> void:
		if not _running:
			return
		_running = false
		cancelled.emit()


	# Starts ticking. Called internally by MultiplayerScene.start_countdown.
	func _start() -> void:
		_running = true
		_schedule_tick()


	func _schedule_tick() -> void:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene) or not scene.is_inside_tree():
			_running = false
			return
		scene.get_tree().create_timer(_tick_interval).timeout.connect(
			_on_tick,
			CONNECT_ONE_SHOT,
		)


	func _on_tick() -> void:
		if not _running:
			return
		_seconds_left -= 1
		tick.emit(_seconds_left)
		if _seconds_left <= 0:
			_running = false
			finished.emit()
		else:
			_schedule_tick()


## Server-side handle for a participant scene move.
##
## Obtain via [method MultiplayerScene.move_participants].
class MoveBatch:
	extends RefCounted

	## Emitted as each participant is admitted to the destination scene.
	signal participant_arrived(participant: NetwParticipant)
	## Emitted after every participant has been admitted.
	signal completed()

	var _pending: int = 0
	var _arrivals: Array[NetwParticipant] = []


	func _init(participants: Array[NetwParticipant]) -> void:
		_pending = participants.size()


	func _queue_arrival(participant: NetwParticipant) -> void:
		_pending = maxi(0, _pending - 1)
		_arrivals.append(participant)


	func _flush() -> void:
		for participant: NetwParticipant in _arrivals:
			participant_arrived.emit(participant)
		_arrivals.clear()
		completed.emit()


## Per-scene readiness gate tracks which participants have confirmed ready.
##
## Obtain via [method MultiplayerScene.create_readiness_gate].
## Clients call [method set_ready]. The server broadcasts the change to all peers.
## [codeblock]
## # Game scene screen (runs on all peers):
## var gate := scene.create_readiness_gate()
## gate.participant_ready_changed.connect(_refresh_ready_ui)
## gate.all_ready.connect(_on_everyone_ready)
##
## # Player clicks "Ready":
## gate.set_ready(true)
## [/codeblock]
class Readiness:
	extends RefCounted

	## Emitted on all peers when a participant's readiness state changes.
	signal participant_ready_changed(participant: NetwParticipant, is_ready: bool)
	## Emitted when every tracked participant is ready.
	##
	## This also emits when a not-ready participant leaves, if the remaining
	## participants are all ready.
	signal all_ready()

	var _scene_ref: WeakRef
	# Peer ID -> ready state. Populated as participants enter or leave.
	var _readiness: Dictionary[int, bool] = { }


	func _init(scene: MultiplayerScene) -> void:
		_scene_ref = weakref(scene)


	## Returns [code]true[/code] while the underlying [MultiplayerScene] is still alive.
	func is_valid() -> bool:
		return is_instance_valid(_scene_ref.get_ref())


	## Returns [code]true[/code] if [param participant] has confirmed ready.
	func is_ready(participant: NetwParticipant) -> bool:
		if participant == null:
			return false
		return _readiness.get(participant.peer_id, false)


	## Returns [code]true[/code] if [param participant] is tracked.
	func tracks(participant: NetwParticipant) -> bool:
		if participant == null:
			return false
		return _readiness.has(participant.peer_id)


	## Returns all participants that have confirmed ready.
	func get_ready_participants() -> Array[NetwParticipant]:
		var result: Array[NetwParticipant] = []
		var tree := _tree()
		if tree == null:
			return result
		for id: int in _readiness:
			if _readiness[id]:
				var participant := tree.participant(id)
				if participant:
					result.append(participant)
		return result


	## Returns [code]true[/code] when every tracked participant is ready and
	## there is at least one participant.
	func are_all_ready() -> bool:
		if _readiness.is_empty():
			return false
		for v: bool in _readiness.values():
			if not v:
				return false
		return true


	## Marks the local participant as ready or not ready.
	##
	## On a client this sends an RPC to the server. On the server/host it applies
	## the change directly. The update is broadcast to all peers automatically.
	func set_ready(ready: bool = true) -> void:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return
		if scene.multiplayer.is_server():
			scene._handle_set_ready(scene.multiplayer.get_unique_id(), ready)
		else:
			scene._rpc_request_set_ready.rpc_id(1, ready)


	# Called internally by MultiplayerScene when the server broadcasts a readiness update.
	func _receive_ready_changed(peer_id: int, is_ready: bool) -> void:
		_readiness[peer_id] = is_ready
		var tree := _tree()
		var participant := tree.participant(peer_id) if tree else null
		if participant:
			participant_ready_changed.emit(participant, is_ready)
		if are_all_ready():
			all_ready.emit()


	# Called internally when a participant enters the scene.
	func _add_peer(peer_id: int) -> void:
		if peer_id not in _readiness:
			_readiness[peer_id] = false


	# Called internally when a participant leaves the scene.
	func _remove_peer(peer_id: int) -> void:
		if _readiness.erase(peer_id) and are_all_ready():
			all_ready.emit()


	func _tree() -> NetwMultiplayer:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return null
		return NetwMultiplayer.of(scene)
