## Public lag-compensation surface for one [MultiplayerTree], owned by
## [NetwMultiplayer].
##
## [member NetwMultiplayer.lag_compensation] is never [code]null[/code]. The
## interface is constructed inert with the [NetwMultiplayer] that owns it and
## activates when a [LagCompensation] node registers through
## [method MultiplayerAPI.object_configuration_add], so a tree with no
## [LagCompensation] node degrades to safe no-op queries and cleanly opts out
## of prediction and rewind. This is the query and action
## surface over server-recorded history. The server records authoritative state
## every tick keyed by the consumed input tick rather than the server clock, so a
## read at a logical tick returns the state that tick's input actually produced.
## That keying is the invariant every read here depends on. [method sample] and
## [method rewind] answer "where was an entity when the shooter saw it", and
## [method action] correlates an optimistic local effect with the authoritative
## result. The client prediction and reconciliation side of the same loop is the
## per-entity engine this interface steps, configured and observed through
## [member NetwEntity.prediction] and declared in a scene with
## [PredictionComponent].
##
## [codeblock]
## var lag := Netw.of(self).lag_compensation
## var past := lag.sample(target_entity, view_tick)   # detached NetwSnapshot
## if past.has_value(&"position"):
##     validate_hit(origin, dir, past.position)
##
## var action := lag.action(_place_bomb)
## action.predict = _predict_bomb
## action.request(view_tick)
## [/codeblock]
##
## [b]The rewind boundary[/b]
## [br]The server records exactly the two streams it must be able to second-guess:
## [constant NetwSyncSet.Record.RECORD_STATE], its own authoritative history, and
## [constant NetwSyncSet.Record.RECORD_INPUT], the controller's claim it replays to
## verify rather than believe. [method register_timeline] fires off state-set
## presence for that reason. A stream whose author is trusted outright records
## nothing, so a [constant NetwSyncSet.Record.RECORD_BROADCAST] set is display
## truth that lives entirely outside this boundary and never grows a timeline.
##
## [br][br][b]The input to state lifecycle[/b]
## [br]The owning client authors input every tick and predicts immediately, then
## ships that input toward the server stamped with its
## [member NetwSyncSetBinding.authored_tick]. The server consumes one input per
## tick through the entity's prediction engine, so its consume frontier trails
## real time by about a half round trip. It records the produced state keyed by
## the consumed input tick. A read therefore names a logical tick of input, not a wall-clock
## moment.
## [codeblock]
## client : author input[t] -> predict state[t] -> record_input(t) / record_state(t)
##              |
##              |  ship input[t]   (input binding authored_tick = t)
##              v
## server : consume one input per tick, frontier trails by ~half a round trip
##              |
##              |  state[t] = simulate(input[t])
##              v
## NetwTimeline (server) keyed by the CONSUMED input tick t, never the clock
##   ┠╴ input[t]  the exact command, never carries forward
##   ┖╴ state[t]  the produced state, carried forward to later reads
## [/codeblock]
##
## [b]Reading history[/b]
## [br][method sample] returns [method NetwTimeline.state_at] for the requested
## tick when the consume frontier has reached it, otherwise
## [method NetwTimeline.latest_state_at_or_before], which carries the most recent
## prior slot forward. A tick ahead of the frontier therefore reads the frontier,
## not a future the server has not simulated. [method rewind] is the opt-in
## heavyweight that applies that past state to live nodes for a real physics query.
## The tick comes from the firing client as
## [method NetwInterpolationInterface.Handle.displayed_authoring_tick], the
## server authored tick it displayed when it acted.
##
## [br][br][b]Action readiness[/b]
## [br]An action evaluated at a view tick agrees with the client only when two
## independent conditions hold.
## [codeblock]
## 1. availability : the consume frontier reached view_tick, so the prediction
##                   engine has consumed through it and
##                   NetwTimeline.state_at(view_tick) is an exact slot
## 2. determinism  : predict(view_tick) == reconstruct(view_tick)
## [/codeblock]
## [constant NetwAction.TimingMode.IMMEDIATE], the default, waits for neither and
## resolves on arrival. [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY]
## waits for availability so [method sample] reads an input-backed slot rather than
## a carried-forward one. Determinism is never waited on. It is a property of the
## placement contract, required only when the action places at the owner's own
## predicted state, and absent when the action instead validates server-recorded
## history of other entities.
##
## [br][br][b]Assumption layers[/b]
## [br]Each capability adds only its own assumptions, so a game adopts the layer it
## touches and no more.
## [br]- [b]None.[/b] With no [LagCompensation] node mounted under the tree this
## interface no-ops, so a client-authoritative game carries nothing.
## [br]- [method sample] and [method rewind] assume a shared logical tick, server
## per-tick recording, bounded retention, and that only the owning client predicts.
## [br]- [method action] with [constant NetwAction.TimingMode.IMMEDIATE] adds
## discrete-event and propose-and-validate semantics.
## [br]- [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] adds the narrow
## assumptions that the action is owner-anchored, that the owner simulation is
## deterministic, and that resolution may be deferred behind the optimistic effect.
class_name NetwLagCompensationInterface
extends RefCounted

const _DEFAULT_EFFECT_TIMEOUT_TICKS := 120
# Tri-state result of the action readiness check, shared by admission and drain.
enum _Readiness { NOT_READY, READY, READY_BY_DEADLINE }

## Maximum number of ticks a player action may be scheduled ahead of the
## server clock before it is denied.
var max_future_action_ticks: int = 8

## Ticks a [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] action waits
## for input-backed state at its view tick before it resolves best-effort.
##
## A state-ready action queues from the tick the server admits it. It resolves the
## moment the server has consumed and recorded authoritative state for the view
## tick, the input-backed slot [method sample] reads. Lost or late input can mean
## that slot never arrives, so this bound stops the wait from lasting forever. Once
## the wait reaches it the action resolves against the best available history and
## [signal action_gate_fallback] fires.
## [codeblock]
## queued_at                              state recorded at view_tick -> resolve (input-backed)
## queued_at + input_gate_deadline_ticks  still no state at view_tick -> resolve best-effort
## [/codeblock]
## Set it above the input arrival lag in ticks, about
## [member NetwClockInterface.recommended_display_offset] for the network and jitter
## part plus the input send cadence. A value below that resolves state-ready actions
## best-effort under normal latency, the artifact the gate exists to prevent.
var input_gate_deadline_ticks: int = 12

## Emitted when a state-ready action resolves through its deadline fallback.
signal action_gate_fallback(key: StringName, view_tick: int)

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef
# Flipped by NetwMultiplayer when a LagCompensation configurator registers.
var _configured := false
# The registered configurator node. Gates the server-authority paths as the
# configured-presence proxy and backs export reads.
var _node: LagCompensation
# The session tick engine, bound by the configurator's clock binding.
var _clock: NetwClockInterface

var _registry := _TimelineRegistry.new()
var _recorder := _HistoryRecorder.new()
var _runner := _SimulationRunner.new()
# Per-entity prediction engine records, created by register_prediction, keyed by
# NetwEntity. The handle on NetwEntity.prediction reaches its record back through
# a weakref, so erasing an entry here is the whole release.
var _engines: Dictionary = { }
var _queries: _RewindQueries
var _effects: Dictionary[StringName, Dictionary] = { }
var _effect_watchers: Dictionary[StringName, Dictionary] = { }
var _pending_actions: Array[_PendingAction] = []
var _action_slots: Dictionary[String, int] = { }
var _observed_entities: Dictionary[NetwEntity, bool] = { }
var _gate_fallbacks: int = 0

## Keyed optimistic effects for [NetwAction] and custom transports.
var effects: NetwEffects:
	get:
		return NetwEffects.new(self)


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	_queries = _RewindQueries.new(_registry)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## True once a [LagCompensation] configurator has registered. Until then every
## query returns its safe empty result and every action is denied.
func is_configured() -> bool:
	return _configured


## Resolves the lag-compensation interface for [param node], logging an error
## when [param node] sits under a [MultiplayerTree] that has no
## [LagCompensation] node mounted.
##
## A node run standalone (no enclosing [MultiplayerTree], for example pressing
## [code]F6[/code] on a scene in isolation) resolves to [code]null[/code] quietly,
## so detached testing keeps working. A node mounted in a real session that depends
## on rewind or prediction but finds no [LagCompensation] is a misconfiguration,
## so [method NetwDbg.error] names it rather than crashing.
static func resolve_required(node: Node) -> NetwLagCompensationInterface:
	var mt := MultiplayerTree.resolve(node)
	if not mt:
		return null
	if mt.api and mt.api.lag_compensation.is_configured():
		return mt.api.lag_compensation
	Netw.dbg.error(
		"%s needs a LagCompensation node mounted under the MultiplayerTree, "
		+ "but none was found. Add one as a child of the tree to enable "
		+ "prediction and rewind.",
		[node.get_class() if node else "A node"],
		func(m: String) -> void: push_error(m),
	)
	return null


## Creates and wires the prediction engine record for [param entity]. Idempotent.
##
## [PredictionComponent] calls this on tree entry after pushing its exports into
## [member NetwEntity.prediction], and a code-first caller configures that handle
## and calls this directly. The engine resolves its role from authority, follows
## [signal NetwEntity.control_changed] and [signal NetwEntity.reparented], and
## steps in the deterministic simulation loop. Only the roles this peer simulates
## enter the loop, so a remote display costs nothing per tick.
func register_prediction(entity: NetwEntity) -> void:
	if not entity or _engines.has(entity):
		return
	var engine := _PredictionEngine.new()
	_engines[entity] = engine
	entity.prediction._engine_ref = weakref(engine)
	engine._attach(self, entity)


## Releases [param entity]'s prediction engine record, restoring the set-handle
## hooks it held. [member NetwEntity.prediction] stays bound and keeps its config
## and counters, so a re-registration resumes where the engine left off.
func unregister_prediction(entity: NetwEntity) -> void:
	var engine := _engines.get(entity) as _PredictionEngine
	if not engine:
		return
	_engines.erase(entity)
	engine._release()
	entity.prediction._engine_ref = null


## Registers [param entity] for server-side authoritative recording, returning its
## [NetwTimeline]. Idempotent: a repeat call returns the existing timeline.
##
## [method NetwSyncPipeline.register_derived] calls this when a server-authored
## state set registers, and the server [PredictionComponent] roles read the same
## timeline back, so the trigger is state-set presence, not prediction. The
## created timeline is published to [member NetwEntity.timeline].
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


## Returns aggregate simulation counters for the debug overlay.
##
## The cumulative counters ([code]corrections[/code], [code]consumed[/code],
## [code]missing[/code], [code]max_replay_depth[/code]) sum since spawn, so a live
## monitor like [LagCompensationMonitor] reads them as deltas over an interval. The
## remaining keys are instantaneous occupancy.
## [codeblock]
## {
##   ┠╴ entities: int          # engine records stepped this tick
##   ┠╴ timelines: int         # rewindable entities recorded this tick
##   ┠╴ corrections: int       # summed reconciliation snaps since spawn
##   ┠╴ max_replay_depth: int  # worst replay window walked
##   ┠╴ consumed: int          # summed inputs the server consumed
##   ┠╴ missing: int           # summed input ticks stepped over as lost
##   ┠╴ pending_actions: int   # actions queued awaiting readiness
##   ┠╴ effects_armed: int     # optimistic effects awaiting confirm or deny
##   ┖╴ gate_fallbacks: int    # state-ready actions resolved best-effort
## }
## [/codeblock]
func metrics() -> Dictionary:
	var result := _runner.metrics()
	result[&"timelines"] = _registry.size()
	result[&"pending_actions"] = _pending_actions.size()
	result[&"effects_armed"] = _effects.size()
	result[&"gate_fallbacks"] = _gate_fallbacks
	return result


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot], the analytic hit-validation read.
##
## Returns an empty [NetwSnapshot] when no [LagCompensation] node is mounted, off
## the server, or for an entity with no retained history at [param tick].
##
## [codeblock]
## var past := Netw.of(self).lag_compensation.sample(target, view_tick)
## if past.has_value(&"position") and hits(origin, dir, past.position):
##     apply_damage(target)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	return _queries.sample(entity, tick)


## Applies each entity's state at [param tick] to its live node for the duration of
## [param body], then restores it. The opt-in heavyweight alternative to
## [method sample] for validation that needs real physics queries. A no-op when no
## [LagCompensation] node is mounted.
##
## [codeblock]
## Netw.of(self).lag_compensation.rewind(targets, view_tick, func() -> void:
##     var hit := space.intersect_ray(query)   # targets at the perceived tick
##     ... )
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
	_queries.rewind(entities, tick, body)


## Returns a [NetwAction] bound to [param authority].
##
## [param authority] must be a method [Callable] on the entity root or one of
## its children. The returned action uses [member effects] for local prediction
## and the mounted [LagCompensation] node for private request transport.
func action(authority: Callable) -> NetwAction:
	var slot := _assign_action_slot(authority) if _configured else 0
	return NetwAction.new(self, authority, slot)


## Advances the simulation loop by one tick: drains admitted actions, steps every
## registered prediction engine record, sweeps effect timeouts, and on the server
## records authoritative history. Driven by the [LagCompensation] configurator's
## [signal NetwClockInterface.on_tick] binding.
func tick_step(delta: float, tick: int) -> void:
	_drain_pending_actions(tick)
	_runner.step(delta, tick)
	_sweep_effect_timeouts(tick)
	# The server holds the truth, so only it records authoritative history.
	var api := _api()
	if _node and api and api.is_server():
		_recorder.record(_registry, _engines, tick)


## Submits a player action request to be resolved.
## [br][br][b]Server Only.[/b]
func submit_action(
		route: int,
		method: StringName,
		view_tick: int,
		data: Variant,
		key: StringName,
		timing_mode: NetwAction.TimingMode,
		requester: int,
) -> void:
	var api := _api() if _node else null
	if requester == 0 and api and api.multiplayer_peer:
		requester = api.get_unique_id()

	var target_path: NodePath = NodePath()
	var mt := api.tree if api else null
	var liveness := api.liveness if api else null
	if mt and liveness:
		var entity := liveness.entity_of(route)
		if entity and is_instance_valid(entity.owner):
			target_path = mt.get_path_to(entity.owner)

	var request := _PendingAction.new(
		target_path,
		method,
		view_tick,
		data,
		key,
		requester,
		timing_mode,
		_current_tick(),
	)
	if not _can_resolve_action(request):
		_deny_action_to(requester, key)
		return
	var current_tick := _current_tick()
	var readiness := _action_readiness(request, current_tick)
	if readiness != _Readiness.NOT_READY:
		_execute_ready_action(request, current_tick, readiness)
		return
	# Not ready yet, so queue it, unless it is scheduled too far ahead to wait.
	if view_tick > current_tick + max_future_action_ticks:
		_deny_action_to(requester, key)
		return
	_pending_actions.append(request)


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
		timing_mode: NetwAction.TimingMode,
) -> void:
	if not _node:
		return
	# Routes are allocated server-side and learned from the spawn packet, so a
	# remote requester only ever reads. A client-minted route would name a
	# different entity on the server. A route of 0 fails resolution there and
	# the request is denied.
	var api := _api()
	var is_remote := api and api.multiplayer_peer \
			and not api.is_server()
	var route := 0
	var mt := api.tree if api else null
	var node := mt.get_node_or_null(target_path) if mt else null
	var entity := NetwEntity.of(node)
	var liveness := api.liveness if api else null
	if entity and liveness:
		route = liveness.route_of(entity)
		if route <= 0 and not is_remote:
			route = liveness.allocate_route(entity)
	if route <= 0:
		Netw.dbg.warn(
			"LagCompensation: action target '%s' has no liveness route; "
			+ "the request will be denied",
			[String(target_path)],
		)

	if is_remote:
		if api:
			var payload := var_to_bytes([
				method, view_tick, data, key, timing_mode,
			])
			api.replication.send_to(
				MultiplayerPeer.TARGET_PEER_SERVER,
				route,
				NetwFrameEnvelope.Channel.ACTION,
				payload,
				true,
			)
		return
	submit_action(route, method, view_tick, data, key, timing_mode, 0)


func _deny_action_to(requester: int, key: StringName) -> void:
	var api := _api() if _node else null
	var local_peer := api.get_unique_id() if api \
			and api.multiplayer_peer else 0
	if requester == 0 or requester == local_peer:
		_effect_discard(key)
		return
	if api and api.multiplayer_peer:
		if requester in api.get_peers():
			api.replication.send_to(
				requester,
				0,
				NetwFrameEnvelope.Channel.LAGCOMP_DENY,
				var_to_bytes(key),
				true,
			)
		else:
			_effect_discard(key)
	else:
		_effect_discard(key)


# Client receive for a denied action. Discards the optimistic effect keyed by
# the denial.
func _handle_deny(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var key: StringName = bytes_to_var(payload)
	_effect_discard(key)


func _handle_action_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api() if _node else null
	if not api or not api.is_server():
		return
	var array = bytes_to_var(payload) as Array
	if array == null or array.size() < 5:
		return
	var method: StringName = array[0]
	var view_tick: int = array[1]
	var data: Variant = array[2]
	var key: StringName = array[3]
	var timing_mode: int = array[4]

	submit_action(
		entity.route,
		method,
		view_tick,
		data,
		key,
		timing_mode,
		sender,
	)


func _node_from_tree_path(path: NodePath) -> Node:
	var api := _api() if _node else null
	var mt := api.tree if api else null
	if not mt:
		return null
	return mt.get_node_or_null(path)


func _can_resolve_action(request: _PendingAction) -> bool:
	var target := _node_from_tree_path(request.target_path)
	return target != null and target.has_method(request.method)


func _drain_pending_actions(tick: int) -> void:
	if _pending_actions.is_empty():
		return
	var waiting: Array[_PendingAction] = []
	for request in _pending_actions:
		if not _can_resolve_action(request):
			_deny_action_to(request.requester, request.key)
			continue
		var readiness := _action_readiness(request, tick)
		if readiness == _Readiness.NOT_READY:
			waiting.append(request)
			continue
		_execute_ready_action(request, tick, readiness)
	_pending_actions = waiting


func _input_readiness(request: _PendingAction, tick: int) -> _Readiness:
	if tick - request.queued_at_tick >= input_gate_deadline_ticks:
		return _Readiness.READY_BY_DEADLINE
	var target := _node_from_tree_path(request.target_path)
	var entity := NetwEntity.of(target) if target else null
	if not entity:
		return _Readiness.NOT_READY
	var engine := _engines.get(entity) as _PredictionEngine
	if engine and not engine.has_consumed_state_tick(request.view_tick):
		return _Readiness.NOT_READY
	var timeline := timeline_of(entity)
	if not timeline:
		return _Readiness.NOT_READY
	if timeline.state_at(request.view_tick).is_empty():
		return _Readiness.NOT_READY
	return _Readiness.READY


func _execute_ready_action(
		request: _PendingAction,
		execution_tick: int,
		readiness: _Readiness,
) -> void:
	if readiness == _Readiness.READY_BY_DEADLINE:
		_gate_fallbacks += 1
		action_gate_fallback.emit(request.key, request.view_tick)
	_execute_action(request, execution_tick)


# Single mode dispatch shared by admission and the drain, so both stay mode agnostic.
func _action_readiness(request: _PendingAction, tick: int) -> _Readiness:
	match request.timing_mode:
		NetwAction.TimingMode.IMMEDIATE:
			return _Readiness.READY
		NetwAction.TimingMode.TICK_ALIGNED:
			return _Readiness.READY if request.view_tick <= tick else _Readiness.NOT_READY
		NetwAction.TimingMode.TICK_ALIGNED_STATE_READY:
			if request.view_tick > tick:
				return _Readiness.NOT_READY
			return _input_readiness(request, tick)
	return _Readiness.READY


func _execute_action(request: _PendingAction, execution_tick: int) -> void:
	var target := _node_from_tree_path(request.target_path)
	if not target or not target.has_method(request.method):
		_deny_action_to(request.requester, request.key)
		return
	var clamped_tick := mini(request.view_tick, execution_tick)
	var ctx := NetwAction.Context.new(
		self,
		request.requester,
		clamped_tick,
		request.view_tick,
		execution_tick,
		request.key,
	)
	if request.data == null:
		target.call(request.method, ctx)
	else:
		target.call(request.method, ctx, request.data)


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


class _PendingAction extends RefCounted:
	var target_path: NodePath
	var method: StringName
	var view_tick: int
	var data: Variant
	var key: StringName
	var requester: int
	var timing_mode: int
	var queued_at_tick: int


	func _init(
			p_target_path: NodePath,
			p_method: StringName,
			p_view_tick: int,
			p_data: Variant,
			p_key: StringName,
			p_requester: int,
			p_timing_mode: int,
			p_queued_at_tick: int,
	) -> void:
		target_path = p_target_path
		method = p_method
		view_tick = p_view_tick
		data = p_data
		key = p_key
		requester = p_requester
		timing_mode = p_timing_mode
		queued_at_tick = p_queued_at_tick


# Per-entity authoritative NetwTimeline registry, the server-side rewind substrate.
# Each entry has exactly one writer (the server), so a timeline is keyed by the
# RefCounted NetwEntity rather than a node path and outlives nothing it does not own.
class _TimelineRegistry extends RefCounted:
	# Keyed by the RefCounted NetwEntity so there is no node-path coupling.
	var _timelines: Dictionary[NetwEntity, NetwTimeline] = { }


	# Registers entity, returning its NetwTimeline. Idempotent: a repeat call
	# returns the existing timeline and publishes it to NetwEntity.timeline.
	func register(entity: NetwEntity) -> NetwTimeline:
		if not entity:
			return null
		var existing := _timelines.get(entity) as NetwTimeline
		if existing:
			return existing
		var tl := NetwTimeline.new()
		_timelines[entity] = tl
		entity.timeline = tl
		return tl


	func of(entity: NetwEntity) -> NetwTimeline:
		return _timelines.get(entity) as NetwTimeline


	func unregister(entity: NetwEntity) -> void:
		_timelines.erase(entity)


	# Count of registered rewindable entities, read by the debug monitor.
	func size() -> int:
		return _timelines.size()


	# The live NetwEntity to NetwTimeline map, iterated by the recorder each tick.
	func all() -> Dictionary[NetwEntity, NetwTimeline]:
		return _timelines


# Steps every registered prediction engine each tick in one deterministic order.
# A stable order by NetwEntity.entity_id means the server consumes every entity
# identically each run, so a replayed trace is reproducible. Capability logic stays
# in the engine record; this owns only ordering and metric aggregation.
class _SimulationRunner extends RefCounted:
	var _engines: Array[_PredictionEngine] = []
	# Sorted view of _engines, rebuilt only when the set changes. The order is
	# stable by entity id, so re-sorting a duplicate every tick was pure waste.
	var _sorted: Array[_PredictionEngine] = []
	var _sort_dirty: bool = true


	func register(engine: _PredictionEngine) -> void:
		if engine not in _engines:
			_engines.append(engine)
			_sort_dirty = true


	func unregister(engine: _PredictionEngine) -> void:
		_engines.erase(engine)
		_sort_dirty = true


	func step(delta: float, tick: int) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.simulate_tick(delta, tick)


	func metrics() -> Dictionary:
		var corrections := 0
		var max_replay := 0
		var consumed := 0
		var missing := 0
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			corrections += handle.corrections
			max_replay = maxi(max_replay, handle.max_replay_depth)
			consumed += handle.consumed_count
			missing += handle.missing_count
		return {
			&"entities": _engines.size(),
			&"corrections": corrections,
			&"max_replay_depth": max_replay,
			&"consumed": consumed,
			&"missing": missing,
		}


	# Stable order by entity id so the server consumes every entity identically each
	# run. Rebuilt only when an engine registers or unregisters, since entity ids
	# are fixed once spawned.
	func _rebuild_sorted() -> void:
		_sorted = _engines.duplicate()
		_sorted.sort_custom(
			func(a: _PredictionEngine, b: _PredictionEngine) -> bool:
				return a.order_key() < b.order_key()
		)
		_sort_dirty = false


# Records every registered entity's authoritative state snapshot after a tick. The
# server holds the truth, so the recorder runs only on server authority and reads
# each entity's state-set snapshot through NetwEntity.state_binding. This gives
# non-predicted state-synced entities rewind history too, without a prediction
# engine.
class _HistoryRecorder extends RefCounted:
	# Records the current snapshot_payload of every entity in registry into its
	# timeline at tick. A consuming engine keys its record at the input-backed tick
	# through _PredictionEngine.history_record_tick.
	func record(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
	) -> void:
		var timelines := registry.all()
		for entity in timelines:
			if not is_instance_valid(entity.owner):
				continue
			# A deactivated entity (a lingering despawn) freezes its history at the
			# despawn boundary instead of recording stale frozen copies, so its
			# retained window ages from the moment it died and expires cleanly when
			# it frees.
			if not entity.owner.can_process():
				continue
			var state := entity.state_binding
			if state:
				var record_tick := tick
				var engine := engines.get(entity) as _PredictionEngine
				if engine:
					record_tick = engine.history_record_tick(tick)
				timelines[entity].record_state(record_tick, state.snapshot_payload())


# Read-only history queries over the registry, the server-side lag-compensation
# surface. Lag compensation is a query, not a system: with the server recording
# authoritative snapshots every tick, answering "where was this entity when the
# shooter saw it" is a timeline read. A consumer of the registry, never a producer.
class _RewindQueries extends RefCounted:
	var _registry: _TimelineRegistry


	func _init(registry: _TimelineRegistry) -> void:
		_registry = registry


	# Returns entity's recorded state at or before tick as a detached NetwSnapshot,
	# reading history without touching the live scene. The snapshot carries forward,
	# so a tick between recordings reads the latest prior state. A view tick older
	# than the retained window, an unregistered entity, or a call off the server all
	# return an empty snapshot.
	func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
		var tl := _registry.of(entity)
		if not tl:
			return NetwSnapshot.new()
		return NetwSnapshot.from_dictionary(tl.latest_state_at_or_before(tick))


	# Briefly applies each entity's state at tick to its live node, runs body, then
	# restores the live state, unconditionally, on return. The opt-in heavyweight
	# for validation that needs real physics. An entity with no retained history at
	# tick is left at its live state and skipped.
	func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
		# Capture only the entities we actually rewind, so restore touches exactly
		# the nodes we moved and an entity with no history stays at its live state.
		var captured: Array[Dictionary] = []
		for entity in entities:
			var state := entity.state_binding
			if not state:
				continue
			var tl := _registry.of(entity)
			if not tl:
				continue
			var snap := tl.latest_state_at_or_before(tick)
			if snap.is_empty():
				continue
			captured.append({ &"entity": entity, &"live": state.snapshot_payload() })
			state.apply_payload(snap)
			_force_update_transform(entity)

		body.call()

		for entry in captured:
			var entity: NetwEntity = entry[&"entity"]
			var state := entity.state_binding
			if not state:
				continue
			state.apply_payload(entry[&"live"])
			_force_update_transform(entity)


	# Pushes a rewound or restored transform into the physics server so a space-state
	# query inside the callable sees it. Godot's direct space state reflects the last
	# physics sync, so this must run after each apply.
	func _force_update_transform(entity: NetwEntity) -> void:
		var node := entity.owner
		if is_instance_valid(node) and node.has_method(&"force_update_transform"):
			node.force_update_transform()


## Per-entity prediction and reconciliation config, owned by
## [member NetwEntity.prediction].
##
## The handle holds the settings a [PredictionComponent] declares in a scene or a
## caller sets in code, plus the live counters [method metrics] reads. The kernel
## that steps this config is the per-entity [NetwLagCompensationInterface._PredictionEngine]
## record, wired by [method register_prediction] and released by
## [method unregister_prediction]. The handle outlives the engine, so its config
## and counters survive a control transfer that re-homes the engine.
## [codeblock]
## var pred := NetwEntity.of(self).prediction
## pred.correction_mode = PredictionComponent.CorrectionMode.SNAP
## pred.divergence_epsilon = 0.05
## if pred.is_registered() and pred.is_reconciling:
##     ...   # a correction is snapping and replaying this frame
## [/codeblock]
class PredictionHandle:
	extends RefCounted

	## Per-entity role, resolved from authority at spawn and on control transfer.
	## Mirrors [enum PredictionComponent.Role] by value.
	enum Role {
		## A remote client controls the entity. Predicts and reconciles on ack.
		PREDICT,
		## The server consumes a remote peer's received input into authoritative state.
		CONSUME,
		## A listen-server host controls its own entity, simulating authoritatively.
		HOST_LOCAL,
		## A remote display. Never simulates here, the interpolator shows it.
		REMOTE,
	}

	## What the server does for a missing input tick. Mirrors
	## [enum PredictionComponent.MissingInput] by value.
	enum MissingInput {
		## No input, no movement. The honest cs-style default.
		STALL,
		## Carry the last input forward over the gap.
		REPEAT_LAST,
	}

	## How a reconciliation correction is applied. Mirrors
	## [enum PredictionComponent.CorrectionMode] by value.
	enum CorrectionMode {
		## Resolve from the body type: REPLAY for a kinematic body, SNAP for a dynamic one.
		AUTO,
		## Restore authoritative state, then replay every unacked input over it.
		REPLAY,
		## Restore authoritative state and stop, with no replay.
		SNAP,
	}

	## The simulation step, defaulting to the entity root's
	## [code]_network_tick(delta, tick, is_fresh)[/code]. Set it to route the step
	## through a delegating node. It is a single [Callable], never a fan-out, so
	## exactly one authoritative step runs per entity per tick.
	var simulate: Callable = Callable()

	## How a correction is applied, a [enum CorrectionMode] value. See
	## [enum PredictionComponent.CorrectionMode] for the scene-facing enum.
	var correction_mode: int = CorrectionMode.AUTO

	## Server policy for a missing input tick, a [enum MissingInput] value. See
	## [enum PredictionComponent.MissingInput] for the scene-facing enum.
	var missing_policy: int = MissingInput.STALL

	## Divergence above which a state receive triggers a correction. A per-property
	## threshold in [member divergence_epsilon_overrides] refines it.
	var divergence_epsilon: float = 0.01

	## Per-property divergence thresholds overriding [member divergence_epsilon],
	## keyed by the state field name.
	var divergence_epsilon_overrides: Dictionary[StringName, float] = { }

	## True while a correction is restoring and replaying. Game-feel code reads it.
	var is_reconciling: bool = false

	## Total corrections applied since spawn.
	var corrections: int = 0

	## Deepest replay window walked by any correction.
	var max_replay_depth: int = 0

	## Inputs the server consumed into authoritative state.
	var consumed_count: int = 0

	## Input ticks the server stepped over as lost.
	var missing_count: int = 0

	## Emitted after a correction, with the divergence that triggered it.
	signal reconciled(error: float)

	## Emitted on every state receive on the owning client, before the trim, with the
	## full divergence including sub-[member divergence_epsilon] values.
	signal state_evaluated(recv_tick: int, ack: int, divergence: float, corrected: bool)

	var _entity_ref: WeakRef
	var _engine_ref: WeakRef


	## Returns the [NetwEntity] this handle configures.
	func entity() -> NetwEntity:
		return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


	## True once an engine record has wired to this handle. Display and readiness code
	## reads this to tell a live predictor from a bare config handle.
	func is_registered() -> bool:
		return _engine() != null


	## Steps prediction or consumption for [param tick]. Forwards to the engine, a
	## no-op when none is wired.
	func simulate_tick(delta: float, tick: int) -> void:
		var engine := _engine()
		if engine:
			engine.simulate_tick(delta, tick)


	## Records one server-consumed input at [param tick] into the server timeline, the
	## direct-feed counterpart of a received input frame. Consume role only.
	func record_server_input(tick: int, input: Dictionary) -> void:
		var engine := _engine()
		if engine:
			engine.record_server_input(tick, input)


	## Returns true when the engine has consumed through [param state_tick]. A handle
	## with no engine, or one not in a consume role, reports ready.
	func has_consumed_state_tick(state_tick: int) -> bool:
		var engine := _engine()
		return engine.has_consumed_state_tick(state_tick) if engine else true


	## Returns the [NetwTimeline] state key for the latest authoritative snapshot at
	## [param fallback_tick], the input-backed tick on a consuming engine.
	func history_record_tick(fallback_tick: int) -> int:
		var engine := _engine()
		return engine.history_record_tick(fallback_tick) if engine else fallback_tick


	## Returns the resolved [enum CorrectionMode]. [constant CorrectionMode.AUTO] never
	## leaks out: this is the concrete mode the reconcile path uses.
	func resolved_correction_mode() -> CorrectionMode:
		var engine := _engine()
		if engine:
			return engine.resolved_correction_mode()
		var body := entity().owner if entity() else null
		return resolve_correction_mode_for(body, correction_mode)


	## Resolves [param mode] against [param body]'s type.
	##
	## [constant CorrectionMode.AUTO] picks [constant CorrectionMode.SNAP] for a
	## dynamic body (a [RigidBody2D] or [RigidBody3D], whose solver cannot be stepped
	## per input) and [constant CorrectionMode.REPLAY] otherwise. An explicit mode
	## passes through. Static so tooling and tests resolve without a wired entity.
	static func resolve_correction_mode_for(body: Node, mode: int) -> CorrectionMode:
		if mode != CorrectionMode.AUTO:
			return mode as CorrectionMode
		if body is RigidBody2D or body is RigidBody3D:
			return CorrectionMode.SNAP
		return CorrectionMode.REPLAY


	## Returns the largest per-property error between a predicted and an authoritative
	## snapshot, or [code]INF[/code] on a missing key. Reported on the divergence
	## signals; the correction decision is [method diverged].
	static func divergence(predicted: Dictionary, authoritative: Dictionary) -> float:
		var worst := 0.0
		for key: StringName in authoritative:
			if not predicted.has(key):
				return INF
			worst = maxf(worst, value_error(predicted[key], authoritative[key]))
		return worst


	## Returns true when any property exceeds its own threshold, reading [param epsilon]
	## refined by the per-property [param overrides]. Per-property because a 3D body
	## mixes units (meters, radians, m·s⁻¹) that no single epsilon can serve.
	static func diverged(
			predicted: Dictionary,
			authoritative: Dictionary,
			epsilon: float,
			overrides: Dictionary,
	) -> bool:
		for key: StringName in authoritative:
			if not predicted.has(key):
				return true
			if value_error(predicted[key], authoritative[key]) > overrides.get(key, epsilon):
				return true
		return false


	## Returns the per-property error between two values. Rotations return radians
	## ([Quaternion] / [Basis] angle), so a rotation property wants its own threshold.
	static func value_error(a: Variant, b: Variant) -> float:
		if a is Vector2 and b is Vector2:
			return (a - b).length()
		if a is Vector3 and b is Vector3:
			return (a - b).length()
		if (a is float or a is int) and (b is float or b is int):
			return absf(float(a) - float(b))
		if a is Quaternion and b is Quaternion:
			return absf((a as Quaternion).angle_to(b))
		if a is Basis and b is Basis:
			return absf(
				(a as Basis).get_rotation_quaternion().angle_to(
					(b as Basis).get_rotation_quaternion(),
				),
			)
		# A whole transform combines position and rotation error; thresholds live per
		# property, so this heuristic only feeds the correction decision.
		if a is Transform3D and b is Transform3D:
			return (a.origin - b.origin).length() + absf(
				a.basis.get_rotation_quaternion().angle_to(b.basis.get_rotation_quaternion()),
			)
		if a is Transform2D and b is Transform2D:
			return (a.origin - b.origin).length() + absf(
				angle_difference(a.get_rotation(), b.get_rotation()),
			)
		return 0.0 if a == b else INF


	func _bind(bound_entity: NetwEntity) -> void:
		_entity_ref = weakref(bound_entity)


	func _engine() -> _PredictionEngine:
		return _engine_ref.get_ref() as _PredictionEngine if _engine_ref else null


#region Prediction engine (kernel)

# Per-entity client-predict / server-consume / host-local / reconcile kernel,
# stepped by _SimulationRunner in deterministic order. It reads its config and
# writes its counters through the PredictionHandle on NetwEntity.prediction, and
# owns the predicted timeline, the consume cursors, and the reconciliation math.
# Fenced so a later NetwPredictionInterface extraction is a wholesale move of this
# region plus PredictionHandle, not a rewrite.
class _PredictionEngine extends RefCounted:
	var _iface_ref: WeakRef
	var _entity: NetwEntity
	var _handle: PredictionHandle
	var _role: PredictionHandle.Role = PredictionHandle.Role.REMOTE
	var _correction: PredictionHandle.CorrectionMode = \
			PredictionHandle.CorrectionMode.REPLAY
	var _state_binding: NetwSyncSetBinding
	var _input_binding: NetwSyncSetBinding
	var _timeline: NetwTimeline
	var _clock: NetwClockInterface
	var _tick_delta: float = 1.0 / 60.0
	var _registered: bool = false

	# Predict cursor.
	var _latest_input_tick: int = -1
	# Consume cursors.
	var _next_input_tick: int = -1
	var _ack: int = -1
	var _last_input: Dictionary = { }


	# Binds to the interface and entity, following control transfer and reparent so
	# the role re-resolves in place.
	func _attach(iface: NetwLagCompensationInterface, entity: NetwEntity) -> void:
		_iface_ref = weakref(iface)
		_entity = entity
		_handle = entity.prediction
		if not entity.control_changed.is_connected(_on_control_changed):
			entity.control_changed.connect(_on_control_changed)
		if not entity.reparented.is_connected(_on_reparented):
			entity.reparented.connect(_on_reparented)
		_rewire()


	# Leaves the loop and restores the set-handle hooks, keeping the handle's config
	# and counters so a re-registration resumes.
	func _release() -> void:
		_unregister_from_loop()
		if _state_binding:
			_state_binding.on_applied = Callable()
			_state_binding.write_gate = true
		if _input_binding:
			_input_binding.on_applied = Callable()
			_input_binding.write_gate = true
			_input_binding.window_timeline = null
		if _entity:
			if _entity.control_changed.is_connected(_on_control_changed):
				_entity.control_changed.disconnect(_on_control_changed)
			if _entity.reparented.is_connected(_on_reparented):
				_entity.reparented.disconnect(_on_reparented)


	func handle() -> PredictionHandle:
		return _handle


	func simulate_tick(delta: float, tick: int) -> void:
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_step(delta, tick)
			PredictionHandle.Role.CONSUME:
				_consume_step(delta, tick)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_step(delta, tick)


	func order_key() -> String:
		return str(_entity.entity_id) if _entity else ""


	func history_record_tick(fallback_tick: int) -> int:
		if _role == PredictionHandle.Role.CONSUME and _ack >= 0:
			return _ack + 1
		return fallback_tick


	func has_consumed_state_tick(state_tick: int) -> bool:
		if _role != PredictionHandle.Role.CONSUME:
			return true
		return _ack >= 0 and _ack + 1 >= state_tick


	func resolved_correction_mode() -> PredictionHandle.CorrectionMode:
		return _correction


	func record_server_input(tick: int, input: Dictionary) -> void:
		if _role != PredictionHandle.Role.CONSUME or not _timeline:
			return
		_timeline.record_input(tick, input)
		if _next_input_tick < 0:
			_next_input_tick = tick


	func _iface() -> NetwLagCompensationInterface:
		return _iface_ref.get_ref() as NetwLagCompensationInterface if _iface_ref else null


	func _on_control_changed(_previous_peer: int, _peer: int) -> void:
		_rewire()


	func _on_reparented(_reparent: NetwEntity.ReparentOpts) -> void:
		_rewire()


	# Resolves the role from current authority and binds the set handles. Idempotent
	# so it re-runs on control transfer.
	func _rewire() -> void:
		if not _entity or not is_instance_valid(_entity.owner):
			return
		var state_binding := _entity.state_binding
		var input_binding := _entity.input_binding
		if not state_binding or not input_binding:
			return
		_state_binding = state_binding
		_input_binding = input_binding

		_unregister_from_loop()
		state_binding.on_applied = Callable()
		state_binding.write_gate = true
		input_binding.on_applied = Callable()
		input_binding.write_gate = true
		input_binding.window_timeline = null
		_timeline = null

		var iface := _iface()
		_clock = iface._clock if iface else null
		if _clock:
			_tick_delta = _clock.ticktime
		if not _handle.simulate.is_valid():
			var root := _entity.owner
			if root and root.has_method(&"_network_tick"):
				_handle.simulate = Callable(root, &"_network_tick")

		_role = _resolve_role()
		_correction = PredictionHandle.resolve_correction_mode_for(
			_entity.owner, _handle.correction_mode,
		)
		_error_on_retained_predicted_props(state_binding.set)
		match _role:
			PredictionHandle.Role.PREDICT:
				# Owning client owns a local predicted timeline. The server's
				# authoritative history lives in the registry, never here.
				_timeline = NetwTimeline.new()
				_entity.timeline = _timeline
				# The input send reads this timeline to build its redundancy window.
				# Safe: the owning client is the input authority and never receives
				# its own input, so it only reads here.
				input_binding.window_timeline = _timeline
				# The authoritative state reconciles against the prediction rather
				# than snapping the predicted body, so the network never overwrites it.
				state_binding.write_gate = false
				state_binding.on_applied = _on_state_frame
				_latest_input_tick = -1
				_register_with_loop()
			PredictionHandle.Role.CONSUME:
				# Server reads the registry timeline; received input records into it.
				_timeline = _registry_timeline()
				input_binding.write_gate = false
				input_binding.on_applied = _on_input_frame
				_next_input_tick = -1
				_ack = -1
				_last_input = { }
				_register_with_loop()
			PredictionHandle.Role.HOST_LOCAL:
				_timeline = _registry_timeline()
				_register_with_loop()
			PredictionHandle.Role.REMOTE:
				pass


	# Server roles read the registry-owned timeline, get-or-creating it so the order
	# of state-set registration and engine wiring does not matter.
	func _registry_timeline() -> NetwTimeline:
		var iface := _iface()
		return iface.register_timeline(_entity) if iface else null


	func _error_on_retained_predicted_props(set: NetwSyncSet) -> void:
		for field in set.fields:
			if field.lane == NetwSyncSet.Lane.RETAINED:
				push_error(
					(
							"PredictionComponent: predicted state property '%s' "
							+ "is retained. Predicted state must be volatile so "
							+ "timeline snapshots arrive atomically."
					) % [field.key],
				)


	func _resolve_role() -> PredictionHandle.Role:
		var is_server := _entity.is_authority
		if _entity.is_controlled_locally:
			return PredictionHandle.Role.HOST_LOCAL if is_server \
					else PredictionHandle.Role.PREDICT
		if is_server:
			return PredictionHandle.Role.CONSUME
		return PredictionHandle.Role.REMOTE


	# --- Predict (owning client) ---

	func _predict_step(delta: float, tick: int) -> void:
		var input := _input_binding.snapshot_payload()
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		_run(input, delta, tick, true)
		_timeline.record_state(tick + 1, _capture())


	# Drives reconciliation from a received state frame. The set handle fires this
	# with the decoded header after the authoritative row arrives (the predicted body
	# is not snapped, since the predict binding is write-gated).
	func _on_state_frame(header: Dictionary) -> void:
		_on_state(
			int(header.get("tick", -1)),
			int(header.get("ack", -1)),
			header.get("payload", { }),
		)


	func _on_state(recv_tick: int, ack: int, payload: Dictionary) -> void:
		if ack < 0:
			return
		# Floor the input window so a sample the server has consumed is never re-sent.
		_input_binding.window_floor = maxi(_input_binding.window_floor, ack)
		var predicted := _timeline.latest_state_at_or_before(ack + 1)
		# An empty prediction (nothing recorded at or before the ack) forces a
		# correction; otherwise the per-property thresholds decide.
		var divergence := INF
		var corrected := true
		if not predicted.is_empty():
			divergence = PredictionHandle.divergence(predicted, payload)
			corrected = PredictionHandle.diverged(
				predicted,
				payload,
				_handle.divergence_epsilon,
				_handle.divergence_epsilon_overrides,
			)

		if corrected:
			# Re-resolve from the handle so a runtime correction_mode change is live.
			_correction = PredictionHandle.resolve_correction_mode_for(
				_entity.owner, _handle.correction_mode,
			)
			_handle.is_reconciling = true
			_handle.corrections += 1
			_restore(payload)
			# Anchor the authoritative state at its keyed tick so a later packet
			# carrying the same ack compares against the corrected value, not the
			# stale prediction it just replaced. Without this a duplicate ack (the
			# server is input starved and re-sends the same ack) re-triggers this
			# correction every tick until the ack advances past the stale entry.
			_timeline.record_state(ack + 1, payload)
			# REPLAY re-runs unacked inputs over the restored state (kinematic). SNAP
			# stops at the restore (dynamic): the predicted body resumes forward from
			# truth next tick and the display chase absorbs the snap, since a solver
			# body cannot be stepped per input without a physics fork.
			if _correction == PredictionHandle.CorrectionMode.REPLAY:
				var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
				_handle.max_replay_depth = maxi(_handle.max_replay_depth, window.size())
				var live_input := _input_binding.snapshot_payload()
				for entry in window:
					_run(entry["input"], _tick_delta, entry["tick"], false)
					_timeline.record_state(entry["tick"] + 1, _capture())
				_input_binding.apply_payload(live_input)
			_handle.reconciled.emit(divergence)
			_handle.is_reconciling = false

		_timeline.trim_before(ack)
		_handle.state_evaluated.emit(recv_tick, ack, divergence, corrected)


	# --- Host-local (listen-server host controlling its own entity) ---

	func _host_local_step(delta: float, tick: int) -> void:
		# The host is the authority and the controller at once, so it simulates from
		# its own gathered input and publishes the result. No prediction, no
		# reconciliation against itself.
		var input := _input_binding.snapshot_payload()
		if _timeline:
			_timeline.record_input(tick, input)
		_run(input, delta, tick, true)
		_state_binding.authored_tick = tick
		_state_binding.reconcile_ack = tick
		# Authoritative state is recorded by the recorder after the tick.


	# --- Consume (server) ---

	func _on_input_frame(header: Dictionary) -> void:
		# Record every sample of the received redundancy window into the server
		# timeline, healing a lost input tick, then open the consume cursor on the
		# oldest tick of the first window so the server consumes the stream from its
		# start. Recording is idempotent per tick.
		var samples: Array = header.get("samples", [])
		if samples.is_empty():
			var tick := int(header.get("tick", -1))
			if tick >= 0:
				samples = [{"tick": tick, "payload": header.get("payload", { })}]
		var oldest := -1
		for sample: Dictionary in samples:
			var stick := int(sample.get("tick", -1))
			if stick < 0:
				continue
			_timeline.record_input(stick, sample.get("payload", { }))
			oldest = stick if oldest < 0 else mini(oldest, stick)
		if _next_input_tick < 0 and oldest >= 0:
			_next_input_tick = oldest


	func _consume_step(delta: float, server_tick: int) -> void:
		if _next_input_tick >= 0:
			_consume_one(delta)
		_state_binding.authored_tick = server_tick
		_state_binding.reconcile_ack = _ack
		# Authoritative state is recorded by the recorder after the tick.


	func _consume_one(delta: float) -> void:
		if _timeline.has_input_at(_next_input_tick):
			var input := _timeline.input_at(_next_input_tick)
			_run(input, delta, _next_input_tick, true)
			_last_input = input
			_ack = _next_input_tick
			_next_input_tick += 1
			_handle.consumed_count += 1
			return

		# A later input arrived, so this tick's input is genuinely lost. Apply the
		# policy and step over the hole. Otherwise it simply has not arrived yet.
		if _timeline.newest_input_tick() > _next_input_tick:
			_handle.missing_count += 1
			if _handle.missing_policy == PredictionHandle.MissingInput.REPEAT_LAST \
					and not _last_input.is_empty():
				_run(_last_input, delta, _next_input_tick, true)
			# STALL applies no movement at all.
			_ack = _next_input_tick
			_next_input_tick += 1


	# --- Shared ---

	func _run(input: Dictionary, delta: float, tick: int, is_fresh: bool) -> void:
		_input_binding.apply_payload(input)
		if _handle.simulate.is_valid():
			_handle.simulate.call(delta, tick, is_fresh)


	func _capture() -> Dictionary:
		return _state_binding.snapshot_payload()


	func _restore(payload: Dictionary) -> void:
		_state_binding.apply_payload(payload)


	func _register_with_loop() -> void:
		var iface := _iface()
		if iface:
			iface._runner.register(self)
			_registered = true


	func _unregister_from_loop() -> void:
		var iface := _iface()
		if iface and _registered:
			iface._runner.unregister(self)
		_registered = false

#endregion
