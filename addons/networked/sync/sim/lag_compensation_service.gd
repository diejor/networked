@tool
## Session service that owns temporal networking infrastructure for one tree.
##
## Lag compensation is one feature with several focused helpers, not several public
## nodes. This service composes them and is the only mounted node: the
## [SimulationRunner] steps prediction in deterministic order, the
## [TimelineRegistry] owns per-entity history, the [HistoryRecorder] records
## authoritative state each tick, and [RewindQueries] answers
## [method sample] and [method rewind]. It must never absorb entity-specific
## prediction logic, which stays in [PredictionComponent].
##
## [br][br][b]Per-tick flow[/b]
## [codeblock]
## MultiplayerClock.on_tick
##   └─ LagCompensationService._on_tick(delta, tick)
##        runner.step(delta, tick)            # predict or consume, per role
##        if server: recorder.record(tick)    # snapshot authoritative state
## [/codeblock]
##
## [br][b]Timeline registry[/b]
## [br]The server keeps one [NetwTimeline] per entity as the rewind substrate. A
## [StateSynchronizer] registers its entity through [method register_timeline] when
## it spawns, so an entity is rewindable by default without a [PredictionComponent].
## [method timeline_of] is the query seam, and [method sample] reads it.
##
## Registered through [NetwServices] per [MultiplayerTree], like [MultiplayerClock]
## and [InterestService], so several trees in one [SceneTree] each get their own
## loop. Reach it through [member NetwContext.lag_compensation], not by node lookup.
## Mount it as a sibling of the clock under the session root.
class_name LagCompensationService
extends Node

# Caps the per-frame clock-bind retry so a tree that never mounts a clock stops
# polling. The clock can register after this service, which auto-registers before
# any clock exists, so the bind retries until it appears.
const _MAX_BIND_ATTEMPTS := 600
const _DEFAULT_EFFECT_TIMEOUT_TICKS := 120

var _clock: MultiplayerClock
var _bind_attempts: int = 0
var _registry: TimelineRegistry = TimelineRegistry.new()
var _recorder: HistoryRecorder = HistoryRecorder.new()
var _runner: SimulationRunner = SimulationRunner.new()
var _queries: RewindQueries
var _effects: Dictionary[StringName, Dictionary] = { }
var _effect_watchers: Dictionary[StringName, Dictionary] = { }
var _action_slots: Dictionary[String, int] = { }
var _observed_entities: Dictionary[NetwEntity, bool] = { }

## Keyed optimistic effects for [NetwAction] and custom transports.
var effects: NetwEffects:
	get:
		return NetwEffects.new(self)


func _init() -> void:
	_queries = RewindQueries.new(_registry)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	var mt := NetwServices.register(self, LagCompensationService)
	if not is_instance_valid(mt):
		return
	if not mt.session_entered.is_connected(_on_session_entered):
		mt.session_entered.connect(_on_session_entered)
	if mt.is_online():
		_on_session_entered.call_deferred()
	var tree := get_tree()
	if tree and not tree.node_added.is_connected(_on_node_added):
		tree.node_added.connect(_on_node_added)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var tree := get_tree()
	if tree and tree.node_added.is_connected(_on_node_added):
		tree.node_added.disconnect(_on_node_added)
	_unbind_clock()
	NetwServices.unregister(self, LagCompensationService)


## Registers [param pc] so it is stepped each tick. Idempotent.
##
## The owning client's predictor and the server's consumer register here. Remote
## displays do not, so the loop only ever steps entities this peer simulates.
func register(pc: PredictionComponent) -> void:
	_runner.register(pc)


## Removes [param pc] from the simulation loop.
func unregister(pc: PredictionComponent) -> void:
	_runner.unregister(pc)


## Registers [param entity] for server-side authoritative recording, returning its
## [NetwTimeline]. Idempotent: a repeat call returns the existing timeline.
##
## A [StateSynchronizer] calls this when it spawns, and the server
## [PredictionComponent] roles read the same timeline back, so the trigger is
## state-sync presence, not prediction. The created timeline is published to
## [member NetwEntity.timeline].
##
## [br][br][b]Server Only.[/b]
func register_timeline(entity: NetwEntity) -> NetwTimeline:
	return _registry.register(entity)


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
##
## This is the enumeration seam the server rewind queries read.
func timeline_of(entity: NetwEntity) -> NetwTimeline:
	return _registry.of(entity)


## Drops [param entity]'s timeline from the registry.
func unregister_timeline(entity: NetwEntity) -> void:
	_registry.unregister(entity)


## Returns aggregate simulation counters for the debug overlay. See
## [method SimulationRunner.metrics].
func metrics() -> Dictionary:
	return _runner.metrics()


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot]. See [method RewindQueries.sample].
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	return _queries.sample(entity, tick)


## Applies each entity's state at [param tick] to its live node for the duration of
## [param body], then restores it. See [method RewindQueries.rewind].
##
## [br][br][b]Server Only.[/b]
func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
	_queries.rewind(entities, tick, body)


func _effect_arm(
		key: StringName,
		revert: Callable,
		timeout_ticks: int = 0,
) -> void:
	if key.is_empty():
		return
	var ttl := timeout_ticks
	if ttl <= 0:
		ttl = _DEFAULT_EFFECT_TIMEOUT_TICKS
	_effects[key] = {
		&"revert": revert,
		&"deadline_tick": _current_tick() + ttl,
	}


func _effect_adopt(key: StringName) -> void:
	if not _effects.has(key):
		return
	_effects.erase(key)
	var watcher: Dictionary = _effect_watchers.get(key, { })
	_effect_watchers.erase(key)
	var confirmed: Callable = watcher.get(&"confirmed", Callable())
	if confirmed.is_valid():
		confirmed.call()


func _effect_discard(key: StringName) -> void:
	if not _effects.has(key):
		return
	var entry: Dictionary = _effects[key]
	_effects.erase(key)
	var revert := entry.get(&"revert") as Callable
	if revert and revert.is_valid():
		revert.call()
	var watcher: Dictionary = _effect_watchers.get(key, { })
	_effect_watchers.erase(key)
	var denied: Callable = watcher.get(&"denied", Callable())
	if denied.is_valid():
		denied.call()


func _watch_action(
		key: StringName,
		confirmed: Callable,
		denied: Callable,
) -> void:
	if key.is_empty():
		return
	_effect_watchers[key] = {
		&"confirmed": confirmed,
		&"denied": denied,
	}


func _assign_action_slot(authority: Callable) -> int:
	var target := authority.get_object() as Node
	if not target:
		return 0
	var entity := NetwEntity.of(target)
	if not entity:
		return 0
	var route := "%s:%s" % [entity.entity_id, authority.get_method()]
	if _action_slots.has(route):
		return _action_slots[route]
	var slot := _action_slots.size()
	_action_slots[route] = slot
	return slot


func _send_action_request(
		target_path: NodePath,
		method: StringName,
		view_tick: int,
		data: Variant,
		key: StringName,
) -> void:
	if multiplayer and multiplayer.multiplayer_peer and not multiplayer.is_server():
		_request_action.rpc_id(
			MultiplayerPeer.TARGET_PEER_SERVER,
			target_path,
			method,
			view_tick,
			data,
			key,
		)
		return
	_request_action(target_path, method, view_tick, data, key)


func _deny_action_to(requester: int, key: StringName) -> void:
	var local_peer := multiplayer.get_unique_id() if multiplayer \
			and multiplayer.multiplayer_peer else 0
	if requester == 0 or requester == local_peer:
		_effect_discard(key)
		return
	if multiplayer and multiplayer.multiplayer_peer:
		_deny_action.rpc_id(requester, key)
	else:
		_effect_discard(key)


func _on_session_entered() -> void:
	_bind_attempts = 0
	_try_bind_clock()


# Binds to the tick loop once the clock service exists. The clock can mount after
# this service (which auto-registers before any clock), so a miss reschedules on
# the next frame until the clock appears or the attempt cap is reached.
func _try_bind_clock() -> void:
	if is_instance_valid(_clock):
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return
	var clock := mt.get_service(MultiplayerClock) as MultiplayerClock
	if clock:
		_clock = clock
		if not clock.on_tick.is_connected(_on_tick):
			clock.on_tick.connect(_on_tick)
		return
	_bind_attempts += 1
	if _bind_attempts <= _MAX_BIND_ATTEMPTS and is_inside_tree() \
			and not get_tree().process_frame.is_connected(_try_bind_clock):
		get_tree().process_frame.connect(_try_bind_clock, CONNECT_ONE_SHOT)


func _unbind_clock() -> void:
	if is_instance_valid(_clock) and _clock.on_tick.is_connected(_on_tick):
		_clock.on_tick.disconnect(_on_tick)
	_clock = null


func _on_tick(delta: float, tick: int) -> void:
	_runner.step(delta, tick)
	_sweep_effect_timeouts(tick)
	# The server holds the truth, so only it records authoritative history.
	if multiplayer and multiplayer.is_server():
		_recorder.record(_registry, tick)


@rpc("any_peer", "reliable")
func _request_action(
		target_path: NodePath,
		method: StringName,
		view_tick: int,
		data: Variant,
		key: StringName,
) -> void:
	if multiplayer and not multiplayer.is_server():
		return
	var requester := multiplayer.get_remote_sender_id() if multiplayer else 0
	if requester == 0 and multiplayer and multiplayer.multiplayer_peer:
		requester = multiplayer.get_unique_id()
	var target := _node_from_tree_path(target_path)
	if not target or not target.has_method(method):
		_deny_action_to(requester, key)
		return
	var clamped_tick := mini(view_tick, _current_tick())
	var ctx := NetwAction.Context.new(self, requester, clamped_tick, key)
	if data == null:
		target.call(method, ctx)
	else:
		target.call(method, ctx, data)


@rpc("authority", "reliable")
func _deny_action(key: StringName) -> void:
	_effect_discard(key)


func _node_from_tree_path(path: NodePath) -> Node:
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return null
	return mt.get_node_or_null(path)


func _sweep_effect_timeouts(tick: int) -> void:
	var expired: Array[StringName] = []
	for key: StringName in _effects:
		var entry: Dictionary = _effects[key]
		if int(entry.get(&"deadline_tick", 0)) <= tick:
			expired.append(key)
	for key in expired:
		_effect_discard(key)


func _current_tick() -> int:
	return _clock.tick if is_instance_valid(_clock) else 0


func _on_node_added(node: Node) -> void:
	_observe_node_entity(node)
	call_deferred("_observe_node_entity_ref", weakref(node))


func _observe_node_entity_ref(node_ref: WeakRef) -> void:
	var node := node_ref.get_ref() as Node if node_ref else null
	if not is_instance_valid(node):
		return
	_observe_node_entity(node)


func _observe_node_entity(node: Node) -> void:
	var entity := NetwEntity.of(node)
	if not entity:
		return
	if not entity.entity_id.is_empty():
		_effect_adopt(entity.entity_id)
	if _observed_entities.has(entity):
		return
	_observed_entities[entity] = true
	if not entity.spawned.is_connected(_on_entity_spawned):
		entity.spawned.connect(_on_entity_spawned.bind(entity))


func _on_entity_spawned(entity: NetwEntity) -> void:
	if entity and not entity.entity_id.is_empty():
		_effect_adopt(entity.entity_id)
