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
##   |- input[t]  the exact command, never carries forward
##   `- state[t]  the produced state, carried forward to later reads
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
##
## [br][br][b]The model[/b]
## [br]Prediction is one recurrence: the next state is the transition function
## applied to state, commands, and environment.
## [codeblock]
## S' = F(S, C, E)
##   S  state        the declared state fields, compared against authority
##   C  commands     the declared input fields, one set per tick
##   E  environment  the declared sensor, epoch, and island facts
##   F  transition   the game's simulate callable, run on its schedule cadence
## [/codeblock]
## [code]S[/code] is what [method NetwScriptModel.PropertyConfig.state]
## declares, [code]C[/code] what [method NetwScriptModel.PropertyConfig.input]
## declares, [code]E[/code] what
## [method NetwLagCompensationInterface.PredictionHandle.configure_sensors] and
## [method NetwLagCompensationInterface.PredictionHandle.configure_island]
## declare, and [code]F[/code] is
## [member NetwLagCompensationInterface.PredictionHandle.simulate] run on the
## [enum NetwLagCompensationInterface.PredictionHandle.Schedule] cadence. When a
## transition disagrees with authority,
## [signal NetwLagCompensationInterface.PredictionHandle.divergence_detected]
## charges the disagreement to one antecedent as a
## [enum NetwPredictJournal.Attribution], and a
## [enum NetwLagCompensationInterface.PredictionHandle.RecoveryPolicy] re-bases
## [code]S[/code].
##
## [br][br][b]The recovery guarantee[/b]
## [br]Recoveries are bounded and convergent at every configuration. A recovery
## sequence shrinks the divergence or escalates to one full-closure restore, a
## restore never projects a field along a channel that is itself diverged, and a
## divergence outside the declared domain restores the whole closure. No
## reachable configuration produces a persistent oscillation.
##
## [br][br][b]The transparency bargain[/b]
## [br]The engine explains only what was declared. An undeclared environment
## read leaves its transitions
## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN] and its divergences
## [constant NetwPredictJournal.Attribution.UNATTRIBUTED]. Declaring the
## environment through
## [method NetwLagCompensationInterface.PredictionHandle.configure_sensors],
## [method NetwLagCompensationInterface.PredictionHandle.configure_epoch], and
## [method NetwLagCompensationInterface.PredictionHandle.configure_island] is
## how attribution sharpens from "something differed" to the antecedent that
## differed.
##
## [br][br][b]The ceiling[/b]
## [br]A divergence charged to environment the owner never had, above all
## contact with bodies other peers control, cannot be predicted away by any
## configuration. It can only be recovered from gracefully, or moved by a
## structural choice: masking the contact, simulating the remote body
## speculatively, or a fully steppable simulation that replays through it.
##
## [br][br][b]Choosing an authority model[/b]
## [br]Prediction serves server-authoritative state. A field the server owns
## and the controller foresees is [method NetwScriptModel.PropertyConfig.state]
## plus a [PredictionComponent]. A field its owner simply owns is
## [method NetwScriptModel.PropertyConfig.broadcast], which needs no
## prediction, no reconciliation, and no quantized canonical form, and trusts
## the owner. The two do not mix on one field: a broadcast field under a
## [PredictionComponent], or a predicted entity whose controller carries no
## [PredictionComponent], is a configuration error the validator reports.
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

## Emitted on authority when an owner's claimed post-state for [param entry]
## disagrees with the one authority reached, charged to [param attribution].
##
## Authority holds a timeline per peer and checks it, so a peer whose simulation
## has drifted is discovered where the divergence can be acted on rather than
## only where it is felt. This is a report and never a repair: nothing is pushed
## back to [param peer] on the strength of it, so a game decides for itself
## whether a run of these is latency, a bug, or a client worth distrusting.
## [codeblock]
## api.lag_compensation.peer_divergence.connect(
##     func(peer: int, entry: int, attribution: NetwPredictJournal.Attribution):
##         if attribution == NetwPredictJournal.Attribution.SIMULATION:
##             suspicion[peer] = suspicion.get(peer, 0) + 1
## )
## [/codeblock]
## [br][br][b]Server Only.[/b]
signal peer_divergence(
	peer: int,
	entry: int,
	attribution: NetwPredictJournal.Attribution,
)

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef
# Flipped by NetwMultiplayer when a LagCompensation configurator registers.
var _configured := false
# The session tick engine, bound by the configurator's clock binding.
var _clock: NetwClockInterface

var _registry := _TimelineRegistry.new()
var _recorder := _HistoryRecorder.new()
var _runner := _SimulationRunner.new()
# Per-entity prediction engine records, created by register_prediction, keyed by
# NetwEntity. The handle on NetwEntity.prediction reaches its record back through
# a weakref, so erasing an entry here is the whole release.
var _engines: Dictionary = { }

# Env-gated JSONL drain of the public prediction surface, built lazily the
# first frame it is armed and never re-checked once found off.
const _PredictTap := preload("res://addons/networked/replication/netw_predict_tap.gd")
var _tap = null
var _tap_off: bool = false
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


## Applies [param config] to the engine. Called by [NetwMultiplayer] when a
## [LagCompensation] registers its [NetwLagCompensationConfig] through
## [method MultiplayerAPI.object_configuration_add]. The values live here, not on
## the node, so [method is_configured] stays true after a scene change frees the
## configurator.
@warning_ignore("unused_parameter")
func configure(node: LagCompensation, config: NetwLagCompensationConfig) -> void:
	max_future_action_ticks = config.max_future_action_ticks
	input_gate_deadline_ticks = config.input_gate_deadline_ticks


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
	# Resolve the session api directly, falling back to the enclosing tree when
	# a node's own multiplayer is not yet bound to the api at call time.
	var api := NetwMultiplayer.of(node)
	if api == null:
		var mt := MultiplayerTree.resolve(node)
		api = mt.api if mt else null
	if not api:
		return null
	if api.lag_compensation.is_configured():
		return api.lag_compensation
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
##   |- entities: int          # engine records stepped this tick
##   |- timelines: int         # rewindable entities recorded this tick
##   |- corrections: int       # summed reconciliation snaps since spawn
##   |- max_replay_depth: int  # worst replay window walked
##   |- consumed: int          # summed inputs the server consumed
##   |- missing: int           # summed input ticks stepped over as lost
##   |- pending_actions: int   # actions queued awaiting readiness
##   |- effects_armed: int     # optimistic effects awaiting confirm or deny
##   `- gate_fallbacks: int    # state-ready actions resolved best-effort
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
	_runner.tick_step(PredictTiming.new(tick, delta, _ticktime()))
	_sweep_effect_timeouts(tick)
	# The server holds the truth, so only it records authoritative history.
	var api := _api()
	if _configured and api and api.is_server():
		_recorder.record_tick(_registry, _engines, tick)


## Records the state produced by the preceding FRAME drive after its physics
## solve has completed. Driven before the next frame's tick loop.
func before_frame_step() -> void:
	_runner.before_frame_step()
	var api := _api()
	if _configured and api and api.is_server():
		_recorder.record_frame(_registry, _engines, _current_tick())
	_drain_tap()


## Advances every FRAME-scheduled simulation once after the clock's tick loop.
## Tick callbacks only author and label input for these entities. This pass
## performs their one drive application for the physics frame.
func frame_step() -> void:
	_runner.frame_step(frame_timing())


# Drains every registered entity's public journal and stats to JSONL when the
# NETW_PREDICT_TAP directory is set. The tap reads only the public handle, so
# this never perturbs the run it records.
func _drain_tap() -> void:
	if _tap_off:
		return
	if _tap == null:
		if not _PredictTap.armed():
			_tap_off = true
			return
		_tap = _PredictTap.new()
	for entity: NetwEntity in _engines:
		if is_instance_valid(entity):
			_tap.drain(entity.entity_id, entity.prediction)


## Reads the clock once for one FRAME pass and returns it by value.
##
## A frame drive completes the solve of the tick before the one now opening, so
## the transition it authors is labeled [code]tick - 1[/code]. Callers that step
## an engine directly through
## [method NetwLagCompensationInterface.PredictionHandle.simulate_frame] capture
## here too, so a direct drive and a pumped drive agree about when they are.
func frame_timing() -> PredictTiming:
	if not is_instance_valid(_clock):
		return PredictTiming.new(0, 0.0, 0.0)
	return PredictTiming.new(_clock.tick - 1, _clock.ticktime, _clock.ticktime)


# The fixed network tick duration, or 0.0 when no clock is resolvable. A reader
# that gets 0.0 keeps whatever step it already had rather than adopting a
# meaningless one.
func _ticktime() -> float:
	return _clock.ticktime if is_instance_valid(_clock) else 0.0


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
	var api := _api() if _configured else null
	if requester == 0 and api and api.multiplayer_peer:
		requester = api.get_unique_id()

	var target_path: NodePath = NodePath()
	var anchor := api.root if api else null
	var liveness := api.liveness if api else null
	if anchor and liveness:
		var entity := liveness.entity_of(route)
		if entity and is_instance_valid(entity.owner):
			target_path = anchor.get_path_to(entity.owner)

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
	if not _configured:
		return
	# Routes are allocated server-side and learned from the spawn packet, so a
	# remote requester only ever reads. A client-minted route would name a
	# different entity on the server. A route of 0 fails resolution there and
	# the request is denied.
	var api := _api()
	var is_remote := api and api.multiplayer_peer \
			and not api.is_server()
	var route := 0
	var anchor := api.root if api else null
	var node := anchor.get_node_or_null(target_path) if anchor else null
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
			var payload := var_to_bytes(
				[
					method,
					view_tick,
					data,
					key,
					timing_mode,
				],
			)
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
	var api := _api() if _configured else null
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


func _handle_predict_command_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as _PredictionEngine
	if engine:
		engine.receive_command_frame(payload)


func _handle_predict_ack_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as _PredictionEngine
	if engine:
		engine.receive_ack_frame(payload)


func _handle_action_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var api := _api() if _configured else null
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
	var api := _api() if _configured else null
	var anchor := api.root if api else null
	if not anchor:
		return null
	return anchor.get_node_or_null(path)


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


## The one reading of the clock a single simulation pass gets, taken at the pump
## boundary and handed down by value to everything the pass drives.
##
## A prediction kernel decides from its antecedents alone, so it may not resolve
## a clock of its own. Two kernels in one pass that each asked would be free to
## disagree about which tick they were running, and a replay could reproduce
## neither answer. Capturing once makes the pass's timing an antecedent like the
## command and the previous state, which is what lets
## [method NetwLagCompensationInterface.frame_step] drive a body with no clock in
## reach at all.
## [codeblock]
## clock ──> tick_step / frame_step  ── PredictTiming ──> engine ──> kernels
##             (the only reader)         (by value)      (no clock reference)
## [/codeblock]
class PredictTiming:
	extends RefCounted

	## The transition label this pass authors under. A tick pass carries the tick
	## the clock reported; a frame pass carries the tick whose solve it completes.
	var tick: int

	## The step handed to
	## [member NetwLagCompensationInterface.PredictionHandle.simulate] for this
	## one drive.
	var delta: float

	## The fixed duration of one network tick, the step every replayed transition
	## re-runs at and the unit
	## [member NetwLagCompensationInterface.PredictionHandle.ack_age_ticks]
	## measures. Zero means the pass could not resolve one, so a reader keeps
	## whatever step it already had rather than adopting a meaningless one.
	var ticktime: float


	func _init(p_tick: int = 0, p_delta: float = 0.0, p_ticktime: float = 0.0) -> void:
		tick = p_tick
		delta = p_delta
		ticktime = p_ticktime


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


	func tick_step(timing: PredictTiming) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.network_tick(timing)


	func frame_step(timing: PredictTiming) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.simulate_frame(timing)


	func before_frame_step() -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.finalize_frame_state()


	func metrics() -> Dictionary:
		var corrections := 0
		var max_replay := 0
		var consumed := 0
		var missing := 0
		var folded := 0
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			corrections += handle.corrections
			max_replay = maxi(max_replay, handle.max_replay_depth)
			consumed += handle.consumed_count
			missing += handle.missing_count
			folded += handle.folded_count
		return {
			&"entities": _engines.size(),
			&"corrections": corrections,
			&"max_replay_depth": max_replay,
			&"consumed": consumed,
			&"missing": missing,
			&"folded": folded,
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
	func record_tick(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
	) -> void:
		_record(
			registry,
			engines,
			tick,
			PredictionHandle.Schedule.TICK,
			true,
		)


	func record_frame(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
	) -> void:
		_record(
			registry,
			engines,
			tick,
			PredictionHandle.Schedule.FRAME,
			false,
		)


	func _record(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
			schedule: PredictionHandle.Schedule,
			include_unregistered: bool,
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
					if not engine.uses_schedule(schedule):
						continue
					record_tick = engine.history_record_tick(tick)
					# A consuming engine declines a slot on a tick that consumed no
					# input, so the ack's own slot keeps the state that consume
					# actually produced rather than a coasted body under the same key.
					if record_tick < 0 \
							and not engine.consumed_unslotted_transition():
						continue
				elif not include_unregistered:
					continue
				var payload := state.canonicalize_payload(state.snapshot_payload())
				if record_tick >= 0:
					timelines[entity].record_state(record_tick, payload)
				if engine:
					engine.finalize_recorded_state(payload)


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
	##
	## The role is the name for a pair of [member input_source] and
	## [member sim_mode], derived through [method role_for_axes] rather than
	## chosen on its own, so the name and the two facts behind it cannot
	## disagree.
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

	## Where this peer's copy of the entity gets the command it simulates with.
	##
	## [constant LOCAL] authors the command here. [constant RECEIVED] reads a
	## command another peer authored and sent. [constant PREDICTED] guesses a
	## command nobody sent, which no peer does today.
	## [constant NONE] simulates nothing, so it needs no command.
	enum InputSource {
		LOCAL,
		RECEIVED,
		PREDICTED,
		NONE,
	}

	## What this peer's simulation of the entity means.
	##
	## [constant AUTHORITATIVE] produces the truth other peers reconcile against.
	## [constant SPECULATIVE] produces a guess this peer will reconcile.
	## [constant DISPLAY] runs no simulation and shows received state.
	enum SimMode {
		AUTHORITATIVE,
		SPECULATIVE,
		DISPLAY,
	}

	## Cadence that applies the entity's simulation drive.
	enum Schedule {
		## Apply once for every network tick. This preserves kinematic prediction.
		TICK,
		## Apply once after every physics frame's network tick loop.
		FRAME,
	}

	## How the most recent scheduled drive selected its input.
	enum DriveKind {
		## No scheduled drive has run.
		NONE,
		## A newly authored input label drove the simulation.
		FRESH,
		## The previous input label drove the simulation again.
		REPEAT,
		## The consume cursor held while repeating its acknowledged input.
		HOLD,
		## The consume cursor had no input stream and repeated its acknowledged input.
		STARVED,
		## A fresh input drove after older eligible labels were folded away.
		FOLD_DRIVE,
		## A missing input label drove through [member missing_policy].
		MISSING,
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

	## How a [constant CorrectionMode.SNAP] restore places the authoritative state
	## on the body. Mirrors [enum PredictionComponent.RestoreMode] by value.
	enum RestoreMode {
		## Restore the authoritative state verbatim at its own tick.
		EXACT,
		## Project each derivative-declaring field forward to the present tick by its
		## replicated velocity before restoring, so a dynamic body lands near where
		## it is instead of snapping back to a stale tick.
		EXTRAPOLATED,
	}

	## What kind of body the entity predicts, the one decision the schedule and
	## recovery presets are derived from. Mirrors
	## [enum PredictionComponent.Archetype] by value.
	##
	## An archetype is a floor, not a cage: [method configure_prediction] applies
	## its bundle, and any value declared in the scene or configured afterward
	## overrides the bundled one.
	enum Archetype {
		## No preset. Every knob keeps its own default until declared.
		NONE,
		## A body whose step is a plain callable, re-runnable within one frame.
		## Bundles the [constant Schedule.TICK] cadence, the
		## [constant MissingInput.STALL] hold, and
		## [constant RecoveryPolicy.REBASE_REPLAY].
		KINEMATIC,
		## A solver-integrated body whose step cannot be re-run per input.
		## Bundles the [constant Schedule.FRAME] cadence, the
		## [constant MissingInput.REPEAT_LAST] hold,
		## [constant RecoveryPolicy.REBASE_RECOVER], the
		## [constant RestoreMode.EXTRAPOLATED] projection, and a teleport
		## threshold sized for a body that settles through contacts.
		SOLVER_BODY,
	}

	## How a recovery puts a diverged entity back onto the authoritative timeline,
	## the named form of what [enum CorrectionMode] expressed as a body-type
	## guess.
	##
	## A policy names the strategy rather than the mechanism, so an entity
	## declares what it wants and the engine picks how. [constant REBASE_REPLAY]
	## restores the acknowledged state and re-runs the unacknowledged commands
	## over it. [constant REBASE_RECOVER] restores without replaying, for a body
	## whose step cannot be re-run cheaply. [constant ROLLBACK_SCOPE] restores
	## every declared island member and re-runs them together.
	## [constant DELAY_CLOSED] never speculates, so no divergence can arise.
	## [constant OBSERVE] reports a divergence and repairs nothing.
	enum RecoveryPolicy {
		REBASE_REPLAY,
		REBASE_RECOVER,
		ROLLBACK_SCOPE,
		DELAY_CLOSED,
		OBSERVE,
	}

	## The simulation step, defaulting to the entity root's
	## [code]_network_tick(delta, tick, is_fresh)[/code]. Set it to route the step
	## through a delegating node. It is a single [Callable], never a fan-out, so
	## exactly one authoritative step runs per entity per tick.
	var simulate: Callable = Callable()

	## Cadence that invokes [member simulate], a [enum Schedule] value. FRAME
	## scheduling lets elastic tick events choose input labels while a
	## solver-driven body receives exactly one drive application per physics frame.
	var schedule: int = Schedule.TICK

	## How a correction is applied, a [enum CorrectionMode] value. See
	## [enum PredictionComponent.CorrectionMode] for the scene-facing enum.
	var correction_mode: int = CorrectionMode.AUTO

	## How a [constant CorrectionMode.SNAP] restore lands on the body, a
	## [enum RestoreMode] value. [constant RestoreMode.EXTRAPOLATED] projects each
	## field that declares a [member NetwInterpolate.project_channel] velocity
	## sibling forward to the present tick through [NetwProject]. Defaults to
	## [constant RestoreMode.EXACT]. Ignored under [constant CorrectionMode.REPLAY],
	## whose input replay already advances the body to the present.
	var snap_restore: int = RestoreMode.EXACT

	## Where this peer's copy of the entity gets its command, resolved when the
	## entity wires. Read-only: authority and local control decide it.
	##
	## This and [member sim_mode] are the two independent facts the flat
	## [enum Role] compresses into one name. Reading them separately is what lets
	## a question about one axis be answered without naming a whole role.
	var input_source: InputSource = InputSource.NONE

	## What this peer's simulation of the entity means, resolved when the entity
	## wires. Read-only: authority and local control decide it.
	var sim_mode: SimMode = SimMode.DISPLAY

	## The [enum RecoveryPolicy] declared through [method configure_recovery], or
	## [code]-1[/code] while none has been, in which case
	## [method resolved_recovery_policy] derives one from
	## [member correction_mode].
	var recovery_policy: int = -1

	## The island and environment configuration declared through
	## [method configure_island], [method configure_sensors], and
	## [method configure_epoch], keyed by those verbs' own keys plus an internal
	## [code]declared[/code] marker only [method configure_island] sets.
	##
	## Sensors and an epoch are environment attribution, not a claim of exactness,
	## so declaring them alone leaves every transition
	## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN]. Only naming the island
	## participants through [method configure_island] opts an entity into the
	## fingerprint compare. An entity that declares nothing is out of domain on
	## every transition, the absence of a claim rather than a claim of divergence.
	var island_config: Dictionary = { }

	## Ceiling in ticks on the age a [constant RestoreMode.EXTRAPOLATED] restore
	## projects across. The projection advances by the unacked span
	## [member ack_age_ticks], which grows without bound when the server input
	## cursor falls behind, and a linear projection over a large age lands the body
	## far off a curved path. This caps that span the way
	## [member Handle.max_forecast_ticks] caps the display forecast, so a stale ack
	## never launches the body. Defaults to the same [code]6[/code].
	var max_restore_ticks: int = 6

	## Pose error, in the pose field's own units, above which a recovery restores
	## the whole closure instead of withholding the fields declared
	## [method NetwScriptModel.PropertyConfig.teleport_only].
	##
	## A large error means a genuine desync (a wall bounce, a teleport) rather than
	## a contractive field drifting, and past it the predicted body holds nothing
	## worth keeping. The error is measured over the derivative-declaring fields,
	## so an entity that declares no derivative channel has no pose to measure and
	## every one of its recoveries restores the whole closure.
	var teleport_threshold: float = 2.0

	## Ticks that a [method notify_contact] pauses non-teleport corrections for. A
	## collision makes the predicted and authoritative bodies legitimately differ
	## for a few ticks while both solvers settle, and correcting through that
	## transient fights the solver. A hard desync past [member teleport_threshold]
	## still snaps.
	var collision_cooldown_ticks: int = 6

	## True while the authoritative body is asleep, set through [method set_sleeping].
	## Corrections pause while asleep so a sleeping body is never nudged awake by
	## reconciliation. Set it from the game when the dynamic body sleeps and wakes.
	var sleeping: bool = false

	## Server policy for a missing input tick, a [enum MissingInput] value. See
	## [enum PredictionComponent.MissingInput] for the scene-facing enum.
	var missing_policy: int = MissingInput.STALL

	## How many queued input ticks a [constant Schedule.FRAME] pass may fold into
	## the one drive it applies when a backlog has built up.
	##
	## The client authors one input per tick, so a default of [code]1[/code] holds
	## lockstep. A value above it lets a frame skip past inputs that have already
	## arrived rather than replaying a backlog one frame at a time. Folding never
	## steps over a lost tick, and the folded inputs are counted in
	## [member folded_count] rather than simulated, so a fold is never extra
	## simulation.
	##
	## A [constant Schedule.TICK] entity ignores this. Its consume advances the
	## ack by exactly one per tick, because authority may not run a transition its
	## own clock has not reached.
	var max_consume_per_tick: int = 1

	## Queued input ticks the server keeps standing instead of consuming to empty,
	## a de-jitter buffer. Input arrival rides two free-running peer clocks, so its
	## phase drifts through the server's consume boundary and a zero-slack cursor
	## alternates starved ticks (the entity does not step at all) with drained ones.
	##
	## The depth is a maintained standing target, not a one-time warm-up. A tick
	## whose queue has fallen to the target consumes nothing and rebuilds the slack
	## instead ([member held_count]), and the [member max_consume_per_tick] drain
	## trims a burst back down to the target rather than to zero. So a single slip
	## in either direction is absorbed and repaid rather than spent permanently.
	## [codeblock]
	## span > buffer   ->  consume (and drain toward buffer + 1)
	## 0 < span <= buffer  ->  hold, the slack rebuilds   (held_count)
	## span <= 0       ->  starved, no input exists       (starved_count)
	## [/codeblock]
	## Each tick of depth costs its own tick of input latency, and a hold delays one
	## input by one solver step. [code]0[/code] consumes as inputs arrive, keeping
	## strict lockstep with no slack to absorb drift.
	var consume_buffer_ticks: int = 0

	## [constant Schedule.FRAME] tape transitions the server keeps standing before it
	## replays one per physics frame. Replay starts once the contiguous queue
	## exceeds this depth and then maintains it. A missed arrival therefore causes
	## exactly one held frame when the next packet restores the standing depth.
	var replay_buffer_depth: int = 1

	## Ceiling in ticks on how far the server's consume cursor may fall behind the
	## freshest input before it gives up walking and re-opens at the live edge.
	##
	## The drain never steps over an absent tick, so across a gap the cursor gains
	## one tick per server tick while the controller keeps authoring one per tick.
	## A gap therefore never closes on its own, and the ack stays behind forever,
	## which reads as an entity that is simulated but never reconciled. The most
	## common way to open one is a client whose clock re-anchors after it has
	## already authored input, joining a session that has been running a while.
	## [codeblock]
	## span <= ceiling  ->  walk the cursor, healing holes one at a time
	## span >  ceiling  ->  re-open at newest - consume_buffer_ticks (resync_count)
	## [/codeblock]
	## Input skipped by a resync is second-old and no longer worth simulating, and
	## the count lands in [member skipped_count]. Set [code]0[/code] to disable the
	## recovery and let the cursor walk however far behind it falls.
	var max_consume_lag_ticks: int = 60

	## Ticks the ack lags behind the freshest input this handle knows, updated each
	## tick. On the owning client it is the unacked span the extrapolated restore
	## projects across ([member max_restore_ticks] caps it). On the server it is the
	## consume backlog ([member max_consume_per_tick] drains it). Read it to observe
	## the RC4 ratchet: a healthy link holds it near zero, a growing value means the
	## consume cursor is falling behind.
	var ack_age_ticks: int = 0

	## Divergence above which a state receive triggers a correction. A field that
	## needs its own tolerance declares it with
	## [method NetwScriptModel.PropertyConfig.epsilon], which overrides this
	## default for that field alone.
	var divergence_epsilon: float = 0.01

	## Each state field's own divergence from the most recent authoritative
	## comparison, refreshed on every state receive on the owning client.
	##
	## [signal state_evaluated] carries only the worst field's error, so a set
	## mixing meters, radians, and meters per second cannot identify which field
	## moved. Read this to tell a position fork from a velocity fork, in
	## particular when a field is declared
	## [method NetwScriptModel.PropertyConfig.reconcile_only] or
	## [method NetwScriptModel.PropertyConfig.teleport_only] and so diverges
	## without ever triggering or being restored. Empty until the first
	## comparison.
	var last_field_divergence: Dictionary[StringName, float] = { }

	## Config keys the scene's [PredictionComponent] declared with non-default
	## values, named by the configure-verb key that would restate each one.
	##
	## One fact, one source: a code verb that writes a key the scene already
	## declared is a configuration error, because whichever writer ran last
	## would silently win. The component fills this when it pushes its exports,
	## and every configure verb refuses a key found here.
	var scene_declared: Dictionary[StringName, bool] = { }

	## Ticks by which the prediction compared against the most recent
	## authoritative state was older than the tick that state answers for, or
	## [code]-1[/code] when nothing was recorded to compare at all.
	##
	## A comparison is only honest at zero. The predicted state is read
	## carry-forward, so when the owner recorded nothing at the acked tick the
	## comparison silently falls back to an older prediction and the resulting
	## [member last_field_divergence] mixes real divergence with the distance the
	## body simply travelled in between. Read this alongside every divergence
	## number to tell the two apart. Non-zero on the ticks the owner did not
	## simulate, which is the schedule disagreeing rather than the physics.
	var last_compare_staleness: int = -1


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

	## Server ticks that consumed nothing because the queue was empty, the input
	## stream running dry under the consume cursor. A healthy link holds this at
	## zero once [member consume_buffer_ticks] is standing, so a rising count is
	## the de-jitter buffer being too shallow for the peers' clock phase drift.
	var starved_count: int = 0

	## Server ticks that consumed nothing because the queue had not yet risen back
	## above [member consume_buffer_ticks], the buffer deliberately rebuilding its
	## standing depth. A hold delays one input by one solver step, unlike a
	## [member starved_count] tick, which authors no new state at all.
	var held_count: int = 0

	## Older eligible inputs skipped by a FRAME-scheduled backlog fold. The newest
	## eligible input drives once, so this count never represents extra simulation
	## applications.
	var folded_count: int = 0

	## Monotone count of scheduled drive applications. This fingerprints their
	## ordering without changing simulation behavior.
	var drive_seq: int = 0

	## Input label applied by the most recent scheduled drive.
	var last_drive_label: int = -1

	## Selection kind of the most recent scheduled drive, a [enum DriveKind] value.
	var last_drive_kind: int = DriveKind.NONE

	## Epoch of the active prediction tape, or [code]-1[/code] before one is
	## authored or received. Rewiring starts a new client-authored epoch.
	var tape_epoch: int = -1

	## Index of the newest prediction tape transition authored or received. A
	## [constant Schedule.TICK] entity's transitions are its ticks, so this
	## tracks the newest driven tick there.
	var last_transition_index: int = -1

	## Contiguous owner-lane transitions currently queued on the consuming
	## server at or beyond its replay cursor.
	var tape_queue_depth: int = 0

	## Times the consume cursor gave up walking and re-opened at the live edge,
	## bounded by [member max_consume_lag_ticks]. A healthy session resyncs once at
	## most, when the controller's clock first anchors. A rising count means input
	## is arriving too far ahead of the cursor to consume in order.
	var resync_count: int = 0

	## Input ticks a [member resync_count] jump passed over without simulating.
	## They were already stale by more than [member max_consume_lag_ticks], so the
	## authoritative body skips them rather than replaying second-old intent.
	var skipped_count: int = 0

	## Owner-lane frames authority dropped whole because they were malformed or
	## carried a fresh transition without its command. A frame is never parsed
	## into a shorter one, so this counts frames rather than transitions.
	var frames_dropped_invalid: int = 0

	## Transitions authority holds with their commands, decoded off the owner
	## lane and waiting to be replayed.
	var command_queue_depth: int = 0

	## The highest transition the owner has received an acknowledgement for, or
	## [code]-1[/code] before any. This is the authority lane's confirmation
	## frontier, and it is what floors both lanes' redundancy windows, so a value
	## stuck at [code]-1[/code] means the lane is not arriving rather than that
	## it has nothing to say.
	var ack_confirmed_transition: int = -1

	## Transitions authority ran with a command the owner never authored, as
	## declared on the acknowledgement lane. A silent substitution is the fault
	## this counter exists to make impossible.
	var substituted_count: int = 0

	## Acknowledged transitions the owner reached a fingerprint verdict on, the
	## denominator [member fp_mismatch_count] is unreadable without.
	##
	## Once the acknowledgement lane is live every acknowledged transition counts
	## here, because authority sends the fingerprint of the state its own replay
	## produced. While the lane is dark the owner must reassemble the state from
	## authority frames instead, so a row merged from several frames describes no
	## single moment and is left unverified rather than charged as a divergence.
	var fp_verified_count: int = 0

	## Acknowledged transitions whose prediction fingerprinted unequal to the
	## authority state, counted out of [member fp_verified_count]. On a run where
	## every antecedent of the recurrence matched this stays zero, so a nonzero
	## count names a broken antecedent rather than a tolerance that wants widening.
	##
	## The verdict is the correction trigger for a transition labeled
	## [constant NetwPredictJournal.Domain.IN_DOMAIN], and a report for one that
	## is not. Which label a transition earns is decided by
	## [method configure_island], so an entity that declares no island keeps the
	## tolerance compare it always had.
	var fp_mismatch_count: int = 0

	## The first transition [member fp_mismatch_count] counted, or [code]-1[/code]
	## while none has. This is the transition to inspect, since every later
	## mismatch may be the propagation of this one.
	var first_divergent_transition: int = -1

	## Transitions authority reached a verdict on for the owner's claimed
	## post-state, the denominator [member client_mismatch_count] is unreadable
	## without.
	##
	## The authority-side mirror of [member fp_verified_count]. Authority judges a
	## claim only once its own row for that transition has closed, so a claim that
	## arrives before authority has run the transition waits rather than counting.
	var client_fp_verified_count: int = 0

	## Transitions where the owner's claimed post-state disagreed with the one
	## authority reached, counted out of [member client_fp_verified_count] and
	## reported through
	## [signal NetwLagCompensationInterface.peer_divergence].
	##
	## This counts what authority found, never what it did about it. Authority
	## pushes nothing back on the strength of a mismatch, so a run of these is
	## evidence for a game to weigh rather than a correction already applied.
	var client_mismatch_count: int = 0

	## Which antecedent of the recurrence the most recent divergence was charged
	## to, a [enum NetwPredictJournal.Attribution] value. Meaningful only once
	## [member last_attributed_transition] is not [code]-1[/code].
	##
	## A divergence is always charged to something, so
	## [constant NetwPredictJournal.Attribution.SIMULATION] is a claim and not a
	## default: it says the command hashes matched and the environment digests
	## matched, which leaves the step function itself.
	var last_attribution: NetwPredictJournal.Attribution = \
			NetwPredictJournal.Attribution.UNATTRIBUTED

	## The transition [member last_attribution] describes, or [code]-1[/code]
	## while no divergence has been charged.
	var last_attributed_transition: int = -1

	## Emitted when a transition is found to disagree with authority, before
	## anything is done about it, naming the transition and what the disagreement
	## is charged to.
	##
	## A divergence is reported whether or not it is recovered, so a game that
	## only observes hears about one exactly as loudly as a game that corrects.
	## [codeblock]
	## entity.prediction.divergence_detected.connect(func(entry, attribution):
	##     if attribution == NetwPredictJournal.Attribution.SIMULATION:
	##         push_warning("transition %d diverged under equal antecedents" % entry)
	## )
	## [/codeblock]
	signal divergence_detected(
		entry: int,
		attribution: NetwPredictJournal.Attribution,
	)

	## Emitted after a recovery wrote the body, naming the transition it rebased
	## at, the per-field pose change it applied ([code]after - before[/code],
	## shortest-arc radians for an angle field), whether it was a teleport-tier
	## restore, and what the divergence was charged to.
	##
	## A recovery is one write, so this fires once per recovery and the deltas are
	## the whole of it. A display does not need to absorb them by hand: under
	## [constant NetwInterpolationInterface.PredictedMode.CHASE] the interpolator
	## already turns each one into a decaying render offset, clamped, reset per
	## recovery, and snapped through on a teleport, tuned by
	## [member NetwInterpolationInterface.Handle.chase_glide_time].
	## [codeblock]
	## entity.interpolation.predicted_mode = \
	##         NetwInterpolationInterface.PredictedMode.CHASE
	## entity.interpolation.chase_glide_time = 0.15
	## # the visual glides onto each corrected pose; connect here only to
	## # observe what a recovery wrote
	## [/codeblock]
	signal recovered(
		entry: int,
		deltas: Dictionary,
		teleported: bool,
		attribution: NetwPredictJournal.Attribution,
	)

	## Emitted on every state receive on the owning client, before the trim, with
	## the full divergence including sub-[member divergence_epsilon] values and
	## whether the receive triggered a correction.
	##
	## This is the per-receive companion to [signal divergence_detected], which
	## fires only when a transition actually disagrees and names the transition and
	## its charge. A divergence that grows tick over tick yet never crosses
	## [member divergence_epsilon] is heard here and never there, so telemetry that
	## must observe an unacted drift reads this signal. The scalar is the worst
	## field alone, and [member last_field_divergence] breaks it out per field.
	signal state_evaluated(recv_tick: int, ack: int, divergence: float, corrected: bool)

	var _entity_ref: WeakRef
	var _engine_ref: WeakRef


	## The declared [enum Archetype], or [constant Archetype.NONE] while nothing
	## picked one. Written by [method configure_prediction].
	var archetype: Archetype = Archetype.NONE


	## Declares what kind of body this entity predicts, applying that
	## archetype's schedule and recovery bundle in one call.
	##
	## The archetype is the first decision a predicted entity makes, and the
	## preset is a floor rather than a cage: a key the scene's
	## [PredictionComponent] declared keeps the scene's value, and a
	## [method configure_schedule] or [method configure_recovery] call after
	## this one overrides its part of the bundle.
	## [codeblock]
	## entity.prediction.configure_prediction(
	##     PredictionHandle.Archetype.SOLVER_BODY,
	## )
	## # refine one bundled value afterward:
	## entity.prediction.configure_recovery({ teleport_threshold = 5.0 })
	## [/codeblock]
	func configure_prediction(declared: Archetype) -> void:
		archetype = declared
		var schedule_cfg: Dictionary = { }
		var recovery_cfg: Dictionary = { }
		match declared:
			Archetype.KINEMATIC:
				schedule_cfg = {
					tier = Schedule.TICK,
					hold = MissingInput.STALL,
				}
				recovery_cfg = { policy = RecoveryPolicy.REBASE_REPLAY }
			Archetype.SOLVER_BODY:
				schedule_cfg = {
					tier = Schedule.FRAME,
					hold = MissingInput.REPEAT_LAST,
				}
				recovery_cfg = {
					policy = RecoveryPolicy.REBASE_RECOVER,
					projection = RestoreMode.EXTRAPOLATED,
					teleport_threshold = 3.0,
				}
			_:
				return
		# A scene declaration outranks the preset, so its keys are dropped here
		# rather than tripping the two-source error a caller's restatement earns.
		for key: StringName in scene_declared:
			schedule_cfg.erase(key)
			recovery_cfg.erase(key)
		if not schedule_cfg.is_empty():
			configure_schedule(schedule_cfg)
		if not recovery_cfg.is_empty():
			configure_recovery(recovery_cfg)


	## Declares when this entity simulates and how far behind authority may run.
	##
	## The schedule is one decision with four parts, so it is written once rather
	## than assembled from knobs a caller has to know belong together.
	## [codeblock]
	## entity.prediction.configure_schedule({
	##     tier = PredictionHandle.Schedule.FRAME,
	##     buffer_depth = 1,
	##     hold = PredictionHandle.MissingInput.REPEAT_LAST,
	##     resync_ceiling = 60,
	## })
	## [/codeblock]
	## Keys are optional and an omitted one keeps its current value. An
	## unrecognized key is an error rather than a silent no-op, because a typo in
	## a [Dictionary] is otherwise indistinguishable from a default.
	func configure_schedule(config: Dictionary) -> void:
		var cfg := _validated_config(
			config,
			[&"tier", &"buffer_depth", &"hold", &"resync_ceiling"],
			"configure_schedule",
		)
		if cfg.has(&"tier"):
			schedule = int(cfg[&"tier"])
		if cfg.has(&"buffer_depth"):
			replay_buffer_depth = int(cfg[&"buffer_depth"])
		if cfg.has(&"hold"):
			missing_policy = int(cfg[&"hold"])
		if cfg.has(&"resync_ceiling"):
			max_consume_lag_ticks = int(cfg[&"resync_ceiling"])


	## Declares which entities this one shares a simulation with, so a divergence
	## can be charged to the environment rather than to the simulation.
	##
	## An entity that contacts another is only entitled to reproduce a transition
	## exactly when both peers agree about that other entity too. Declaring the
	## island is how the engine learns which transitions those are, and it is the
	## only verb that opts the entity into the fingerprint compare.
	## [codeblock]
	## entity.prediction.configure_island({
	##     participants = [other_entity],
	##     approximate = false,
	## })
	## [/codeblock]
	## A participant this peer only displays is inequivalent, so contact against
	## it opens a window in which transitions are compared by tolerance instead of
	## by fingerprint. [param approximate] declares every transition that way. The
	## environment half of the island, the world facts a transition runs against,
	## is declared separately through [method configure_sensors] and
	## [method configure_epoch], which never opt the entity into exact compare.
	func configure_island(config: Dictionary) -> void:
		var cfg := _validated_config(
			config,
			[&"participants", &"approximate"],
			"configure_island",
		)
		for key: StringName in cfg:
			island_config[key] = cfg[key]
		# The internal marker the domain label keys exact compare on. Set here and
		# nowhere else, so declaring sensors or an epoch cannot ratchet a lone
		# entity into the fingerprint compare it never asked for.
		island_config[&"declared"] = true


	## Declares the world facts a transition is sampled against, folded into the
	## environment digest that separates an environment divergence from a
	## simulation one. Each entry maps a name to a [Callable] the engine samples
	## before a drive.
	##
	## Declaring sensors does NOT declare an island. A transition stays
	## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN] until
	## [method configure_island] names the bodies this entity contacts, so an
	## entity can attribute its divergences without ratcheting itself into an
	## exact compare it cannot pass.
	## [codeblock]
	## entity.prediction.configure_sensors({
	##     ground = Callable(self, "_sample_ground"),
	## })
	## [/codeblock]
	func configure_sensors(sensors: Dictionary) -> void:
		island_config[&"sensors"] = sensors.duplicate(true)


	## Reads back the value the engine sampled for the declared sensor
	## [param name] before the drive now running, or [param default] when
	## nothing has sampled it.
	##
	## A declared sensor is sampled once per drive and folded into the
	## environment digest, and this read is the other half of that bargain: a
	## drive that reads the sample instead of re-querying the world runs
	## against exactly the facts its digest describes, so an environment
	## divergence can be charged to the environment rather than left
	## unattributed.
	## [codeblock]
	## func _init() -> void:
	##     entity.prediction.configure_sensors({ ground = _sample_ground })
	##
	## func _network_tick(delta: float, tick: int, fresh: bool) -> void:
	##     var ground: Dictionary = entity.prediction.sensor(&"ground", { })
	## [/codeblock]
	func sensor(name: StringName, default: Variant = null) -> Variant:
		var engine := _engine()
		if not engine:
			return default
		return engine._sensor_samples.get(name, default)


	## Declares the version of the static world this entity simulates against.
	## Bumping [param epoch] reopens the tolerance window, since two peers cannot
	## have adopted a world change on the same transition. Like
	## [method configure_sensors] this declares the environment, not an island, so
	## it never opts the entity into the fingerprint compare.
	## [codeblock]
	## entity.prediction.configure_epoch(world_version)
	## [/codeblock]
	func configure_epoch(epoch: int) -> void:
		island_config[&"epoch"] = epoch


	## Declares how a divergence is repaired once one is found.
	## [codeblock]
	## entity.prediction.configure_recovery({
	##     policy = PredictionHandle.RecoveryPolicy.REBASE_REPLAY,
	##     epsilon = 0.35,
	##     teleport_threshold = 3.0,
	##     cooldown_ticks = 6,
	##     projection = PredictionHandle.RestoreMode.EXTRAPOLATED,
	## })
	## [/codeblock]
	## A single field's own threshold is a property fact, declared on the
	## property with [method NetwScriptModel.PropertyConfig.epsilon] rather
	## than here.
	## [code]policy[/code] takes a [enum RecoveryPolicy] value.
	## [constant RecoveryPolicy.REBASE_REPLAY],
	## [constant RecoveryPolicy.REBASE_RECOVER],
	## [constant RecoveryPolicy.DELAY_CLOSED] and
	## [constant RecoveryPolicy.OBSERVE] each run their own mechanism.
	## [constant RecoveryPolicy.ROLLBACK_SCOPE] is accepted and reported through
	## [method resolved_recovery_policy], and rebases the diverged entity alone
	## until it restores the rest of its island.
	##
	## Declaring [constant RecoveryPolicy.DELAY_CLOSED] is a decision about where
	## the entity is simulated at all, not only about how it is repaired. The peer
	## that does not own the timeline resolves [member sim_mode] to
	## [constant SimMode.DISPLAY] and never speculates, so the question of
	## repairing a speculation cannot arise there.
	func configure_recovery(config: Dictionary) -> void:
		var cfg := _validated_config(
			config,
			[
				&"policy", &"epsilon",
				&"teleport_threshold", &"cooldown_ticks", &"projection",
			],
			"configure_recovery",
		)
		if cfg.has(&"policy"):
			recovery_policy = int(cfg[&"policy"])
			correction_mode = _correction_mode_for_policy(
				recovery_policy as RecoveryPolicy,
			)
			# A policy decides where the entity is simulated and not only how it is
			# repaired, so the axes are resolved again here. Waiting for the next
			# control transfer to notice would leave an entity speculating under a
			# policy that says it never speculates.
			var engine := _engine()
			if engine:
				engine._rewire()
		if cfg.has(&"epsilon"):
			divergence_epsilon = float(cfg[&"epsilon"])
		if cfg.has(&"teleport_threshold"):
			teleport_threshold = float(cfg[&"teleport_threshold"])
		if cfg.has(&"cooldown_ticks"):
			collision_cooldown_ticks = int(cfg[&"cooldown_ticks"])
		if cfg.has(&"projection"):
			snap_restore = int(cfg[&"projection"])


	## Returns the [enum RecoveryPolicy] this entity recovers under, resolving
	## [constant CorrectionMode.AUTO] against the body type the way
	## [method resolved_correction_mode] does.
	func resolved_recovery_policy() -> RecoveryPolicy:
		if recovery_policy >= 0:
			return recovery_policy as RecoveryPolicy
		return (
				RecoveryPolicy.REBASE_REPLAY
				if resolved_correction_mode() == CorrectionMode.REPLAY
				else RecoveryPolicy.REBASE_RECOVER
		)


	# Copies the caller's config so a later mutation of their dictionary cannot
	# reach into the handle, and rejects a key this verb does not define. A
	# Dictionary config trades compile-time shape checking for reach, so the
	# check it loses at compile time is owed at apply time. A key the scene
	# already declared is rejected too: the same fact stated in two places has
	# no single source of truth, and the error names the fix instead of letting
	# the later writer silently win.
	func _validated_config(
			config: Dictionary,
			allowed: Array[StringName],
			verb: String,
	) -> Dictionary:
		var out: Dictionary = { }
		for key: StringName in config:
			if not allowed.has(key):
				push_error(
					(
							"PredictionHandle.%s: unknown key '%s'. Known keys "
							+ "are [%s]."
					) % [verb, key, ", ".join(allowed)],
				)
				continue
			if scene_declared.has(key):
				push_error(
					(
							"PredictionHandle.%s: '%s' is declared on the "
							+ "scene's PredictionComponent and configured from "
							+ "code. One source per fact: keep the scene value "
							+ "or the code call, not both. The code value is "
							+ "ignored."
					) % [verb, key],
				)
				continue
			var value: Variant = config[key]
			out[key] = value.duplicate(true) if value is Dictionary \
					or value is Array else value
		return out


	# The correction mechanism a policy runs through. A policy names a strategy
	# and the mechanism is how it is carried out, so several policies can share
	# one mechanism without sharing a meaning.
	func _correction_mode_for_policy(policy: RecoveryPolicy) -> CorrectionMode:
		match policy:
			RecoveryPolicy.REBASE_REPLAY, RecoveryPolicy.ROLLBACK_SCOPE:
				# A scope rollback re-runs its members, so the mechanism is the
				# same replay a lone rebase runs. What the scope adds is who else
				# it re-runs, which the shell decides once the members are known.
				return CorrectionMode.REPLAY
			RecoveryPolicy.REBASE_RECOVER, RecoveryPolicy.DELAY_CLOSED:
				return CorrectionMode.SNAP
			RecoveryPolicy.OBSERVE:
				# Nothing is written under OBSERVE, so the mechanism named here
				# only decides what the recovery would have run had it written.
				return CorrectionMode.SNAP
		return CorrectionMode.SNAP


	## Returns the [enum Role] named by [param source] and [param mode], the
	## compatibility view over the two axes.
	##
	## The axes are the decision and the role is its name, so every role is
	## reachable from some pair and no pair reaches a role that contradicts it.
	## One pair names no role today: a peer speculating on a command it did not
	## author is the shape a future input relay would take, and until one exists
	## it resolves to [constant Role.REMOTE] rather than inventing a fifth name.
	## [codeblock]
	##             AUTHORITATIVE   SPECULATIVE   DISPLAY
	##   LOCAL     HOST_LOCAL      PREDICT       REMOTE
	##   RECEIVED  CONSUME         (unreachable) REMOTE
	##   PREDICTED (unreachable)   (unreachable) REMOTE
	##   NONE      REMOTE          REMOTE        REMOTE
	## [/codeblock]
	static func role_for_axes(source: InputSource, mode: SimMode) -> Role:
		# The simulation axis is asked first. A peer running none needs no
		# command, so where it would have read one cannot change what it is.
		if mode == SimMode.DISPLAY:
			return Role.REMOTE
		if source == InputSource.LOCAL:
			return Role.HOST_LOCAL if mode == SimMode.AUTHORITATIVE \
			else Role.PREDICT
		if source == InputSource.RECEIVED and mode == SimMode.AUTHORITATIVE:
			return Role.CONSUME
		return Role.REMOTE


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
			engine.simulate_tick(NetwLagCompensationInterface.PredictTiming.new(
				tick, delta, _ticktime(),
			))


	## Applies one FRAME-scheduled drive. A no-op for TICK scheduling or when no
	## engine is wired.
	##
	## The transition it authors is labeled from the clock the way
	## [method NetwLagCompensationInterface.frame_step] labels one, so driving an
	## entity directly and letting the pump drive it produce the same transition.
	func simulate_frame(delta: float) -> void:
		var engine := _engine()
		if not engine:
			return
		var timing := _frame_timing()
		timing.delta = delta
		engine.simulate_frame(timing)


	# Captures a FRAME pass's timing from the interface, the shell's job. Falls
	# back to a clock-free reading so a handle stepped outside a session still
	# drives rather than refusing.
	func _frame_timing() -> NetwLagCompensationInterface.PredictTiming:
		var engine := _engine()
		var iface := engine._iface() if engine else null
		if iface:
			return iface.frame_timing()
		return NetwLagCompensationInterface.PredictTiming.new(0, 0.0, 0.0)


	func _ticktime() -> float:
		return _frame_timing().ticktime


	## Returns the [NetwPredictJournal] recording every transition this entity's
	## engine drove, or [code]null[/code] before one is wired.
	##
	## The journal is what a capture, an overlay, or a determinism check reads. It
	## is the engine's own record, so reading it never perturbs the simulation, and
	## its rows outlive the correction that consumed them.
	## [codeblock]
	## var journal := entity.prediction.journal()
	## if journal and journal.first_unmatched() >= 0:
	##     print("unverified from transition ", journal.first_unmatched())
	## [/codeblock]
	func journal() -> NetwPredictJournal:
		var engine := _engine()
		return engine.journal() if engine else null


	## Returns this entity's prediction counters as one record, the schedule,
	## recovery, and verification numbers a capture or an overlay reads together.
	## [codeblock]
	## {
	##  ┠╴consumed (int)          inputs authority folded into its state
	##  ┠╴missing (int)           labels authority never received a command for
	##  ┠╴starved (int)           authority frames with an empty queue
	##  ┠╴held (int)              authority frames rebuilding their standing buffer
	##  ┠╴folded (int)            older eligible labels a fold skipped
	##  ┠╴corrections (int)       recoveries applied since spawn
	##  ┠╴resync (int)            times the consume cursor re-opened at the edge
	##  ┠╴skipped (int)           transitions a resync passed over
	##  ┠╴max_replay_depth (int)  deepest replay window any recovery walked
	##  ┠╴command_queue_depth (int)  transitions authority holds with commands
	##  ┠╴ack_confirmed (int)     highest transition the owner saw acked, or -1
	##  ┠╴frames_dropped_invalid (int)  owner-lane frames dropped whole
	##  ┠╴substituted (int)       transitions authority declared substituted
	##  ┠╴fp_verified (int)       acked transitions the owner could check at all
	##  ┠╴fp_mismatches (int)     of those, the ones that fingerprinted unequal
	##  ┠╴first_divergent_transition (int)   the first of them, or -1
	##  ┠╴client_fp_verified (int)  transitions authority judged the owner's claim on
	##  ┖╴client_mismatches (int)   of those, the ones the owner claimed differently
	## }
	## [/codeblock]
	func stats() -> Dictionary:
		return {
			&"consumed": consumed_count,
			&"missing": missing_count,
			&"starved": starved_count,
			&"held": held_count,
			&"folded": folded_count,
			&"corrections": corrections,
			&"resync": resync_count,
			&"skipped": skipped_count,
			&"max_replay_depth": max_replay_depth,
			&"command_queue_depth": command_queue_depth,
			&"ack_confirmed": ack_confirmed_transition,
			&"frames_dropped_invalid": frames_dropped_invalid,
			&"substituted": substituted_count,
			&"fp_verified": fp_verified_count,
			&"fp_mismatches": fp_mismatch_count,
			&"first_divergent_transition": first_divergent_transition,
			&"client_fp_verified": client_fp_verified_count,
			&"client_mismatches": client_mismatch_count,
		}


	## Returns the prediction tape in ascending transition order. The owning
	## client returns authored transitions. The consuming server returns decoded
	## transitions. A [constant Schedule.TICK] entity's entries are degenerate:
	## index, label, and tick are the same number and every entry is fresh.
	func tape_transitions() -> Array[Dictionary]:
		var engine := _engine()
		return engine.tape_transitions() if engine else [] as Array[Dictionary]


	## Returns the exact predicted state produced by [param transition], or an
	## empty [Dictionary] before its post-solve capture is available.
	func transition_state_at(transition: int) -> Dictionary:
		var engine := _engine()
		return engine.transition_state_at(transition) if engine else { }


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


	## Opens a [member collision_cooldown_ticks] window during which a recovery
	## below [member teleport_threshold] is paused, so a contact transient is not
	## corrected through. Call it from the
	## controlling client when the predicted body registers a collision. A no-op with
	## no engine wired.
	func notify_contact() -> void:
		var engine := _engine()
		if engine:
			engine.notify_contact()


	## Returns the [NetwTimeline] state key for the latest authoritative snapshot at
	## [param fallback_tick], the input-backed tick on a consuming engine, or
	## [code]-1[/code] when this tick consumed no input and so authored no state
	## worth keying. A caller recording history skips a negative key, leaving the
	## slot the last real consume wrote.
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
	## signals; the correction decision is [method diverged]. A key in
	## [param angles] compares as a wrapped angle, so a heading crossing
	## [code]+/- PI[/code] reads
	## as its true arc instead of a full turn.
	static func divergence(
			predicted: Dictionary,
			authoritative: Dictionary,
			angles: Dictionary = { },
	) -> float:
		var worst := 0.0
		for key: StringName in authoritative:
			if not predicted.has(key):
				return INF
			worst = maxf(
				worst,
				_error(predicted[key], authoritative[key], angles.has(key)),
			)
		return worst


	## Fills [param out] with each field's own divergence and returns the worst of
	## them, the same value [method divergence] reports.
	##
	## The scalar alone cannot say which field diverged, and a state set mixing
	## Meters, radians, and meters per second diverge differently per field. A
	## caller
	## diagnosing why corrections fire (or do not) reads the breakdown.
	## [param out] is cleared and refilled, so one dictionary can be reused across
	## receives instead of allocating per comparison.
	static func divergence_by_field(
			predicted: Dictionary,
			authoritative: Dictionary,
			angles: Dictionary,
			out: Dictionary,
	) -> float:
		out.clear()
		var worst := 0.0
		for key: StringName in authoritative:
			var error := INF
			if predicted.has(key):
				error = _error(predicted[key], authoritative[key], angles.has(key))
			out[key] = error
			worst = maxf(worst, error)
		return worst


	## Returns true when any property exceeds its own threshold, reading [param epsilon]
	## refined by the per-property [param overrides]. Per-property because a 3D body
	## mixes units (meters, radians, meters per second) that no epsilon can serve.
	##
	## A key in [param excludes] never triggers on its own: it is skipped here so a
	## field declared [method NetwScriptModel.PropertyConfig.reconcile_only] does
	## not force a correction, though a correction some other field triggers
	## still restores it.
	## A key in [param angles] compares as a wrapped angle
	## ([method @GlobalScope.angle_difference]), so a heading crossing +/- PI never
	## fakes a near-full-turn divergence.
	static func diverged(
			predicted: Dictionary,
			authoritative: Dictionary,
			epsilon: float,
			overrides: Dictionary,
			excludes: Dictionary = { },
			angles: Dictionary = { },
	) -> bool:
		for key: StringName in authoritative:
			if excludes.has(key):
				continue
			if not predicted.has(key):
				return true
			var error := _error(predicted[key], authoritative[key], angles.has(key))
			if error > overrides.get(key, epsilon):
				return true
		return false


	# value_error refined by the field's angle flag: a wrapped-angle float compares
	# by its shortest arc, everything else falls through to value_error.
	static func _error(a: Variant, b: Variant, is_angle: bool) -> float:
		if is_angle and (a is float or a is int) and (b is float or b is int):
			return absf(angle_difference(float(a), float(b)))
		return value_error(a, b)


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
# Wire layouts for the prediction owner and authority lanes, the two channels
# that carry a transition and the fact that authority ran it.
#
# The COMMAND frame's one structural rule is that a fresh transition and its
# command payload are inseparable. The payload section carries exactly one
# payload per fresh transition in the declared range, in order, so a frame that
# lost one cannot be parsed into a shorter one. The decoder returns nothing for
# such a frame rather than reporting a transition whose command it does not
# have, which is what lets the replay path stop substituting silently.
#
# Both layouts are window-redundant: every send repeats the range still in
# flight, floored by what the other side has confirmed, so a lost datagram
# heals on the next send instead of on a retransmit.
#
#   COMMAND (owner -> authority, unreliable)
#     [ack_of_acks varint]     highest ack record the owner has seen
#     [epoch u8][base varint][count u8]
#     [transition bits]        zigzag label delta << 1 | fresh, per transition
#     [payload section]        one windowed payload per FRESH transition
#     [fp section]             count u8, then [post_fp u32][e_digest u32] per
#                              transition the owner has closed
#
#   ACK (authority -> owner, unreliable)
#     [base varint][count u8]
#     per transition: [c_hash u32][post_fp u32][flags u8]
class _PredictFrames extends RefCounted:
	# Set on an ack record when authority ran a command the owner never authored.
	const ACK_SUBSTITUTED := 1 << 0

	# Set on an ack record when a later truth replaced an earlier verdict.
	const ACK_SUPERSEDED := 1 << 1

	const _MAX_RUN := 255
	const _U32 := 0xFFFFFFFF


	# Writes one COMMAND frame. [param transitions] are contiguous
	# {index, label, fresh} rows. [param payloads] holds one value array per fresh
	# row in the same order, encoded through [param quantizers] and [param types].
	# The fingerprint section carries the owner's post-state digest beside the
	# environment it ran against, the same pair the ACK frame carries in the other
	# direction and for the same reason: a disagreement neither peer can charge to
	# the command or to the world is the one charged to the simulation, and
	# authority cannot make that separation from a post-state alone.
	static func encode_command(
			epoch: int,
			ack_of_acks: int,
			transitions: Array,
			payloads: Array,
			quantizers: Array,
			types: Array,
			post_fps: PackedInt32Array = PackedInt32Array(),
			e_digests: PackedInt32Array = PackedInt32Array(),
	) -> PackedByteArray:
		var w := NetwBitBuffer.Writer.new()
		# Zigzag, because the frontier is -1 until the first acknowledgement
		# lands and a plain varint would carry that across as a huge positive.
		NetwCodec.put_varint(w, _encode_zigzag(ack_of_acks))
		var count := mini(transitions.size(), _MAX_RUN)
		var first := maxi(0, transitions.size() - count)
		w.put_aligned_u8(epoch & 0xFF)
		var base := 0
		if count > 0:
			base = int((transitions[first] as Dictionary).get("index", 0))
		NetwCodec.put_varint(w, base)
		w.put_aligned_u8(count)
		var previous_label := 0
		for i in range(first, transitions.size()):
			var row := transitions[i] as Dictionary
			var label := int(row.get("label", -1))
			var tagged := _encode_zigzag(label - previous_label) << 1
			if bool(row.get("fresh", false)):
				tagged |= 1
			NetwCodec.put_varint(w, tagged)
			previous_label = label
		for values: Array in payloads:
			NetwScriptModel.write_values(w, values, quantizers, types)
		var fp_count := mini(mini(post_fps.size(), e_digests.size()), _MAX_RUN)
		w.put_aligned_u8(fp_count)
		for i in fp_count:
			w.put_aligned_u32(post_fps[i] & _U32)
			w.put_aligned_u32(e_digests[i] & _U32)
		return w.to_bytes()


	# Reads one COMMAND frame into
	# {epoch, ack_of_acks, transitions, payloads, post_fps}, or an empty
	# [Dictionary] when the frame is malformed or short one payload. The caller
	# counts a drop and moves on, since a partial frame has no honest reading.
	static func decode_command(
			payload: PackedByteArray,
			quantizers: Array,
			types: Array,
	) -> Dictionary:
		if payload.is_empty():
			return { }
		var r := NetwBitBuffer.Reader.new(payload)
		var ack_of_acks := _decode_zigzag(NetwCodec.get_safe_varint(r))
		var epoch := r.get_aligned_u8()
		var base := NetwCodec.get_safe_varint(r)
		if base < 0:
			return { }
		var count := r.get_aligned_u8()
		var transitions: Array = []
		var previous_label := 0
		var fresh_count := 0
		for offset in count:
			var tagged := NetwCodec.get_safe_varint(r)
			if tagged < 0:
				return { }
			var label := previous_label + _decode_zigzag(tagged >> 1)
			var fresh := bool(tagged & 1)
			if fresh:
				fresh_count += 1
			transitions.append(
				{ "index": base + offset, "label": label, "fresh": fresh },
			)
			previous_label = label
		var payloads: Array = []
		for i in fresh_count:
			var values := NetwScriptModel.read_values(r, quantizers, types)
			if values.size() != quantizers.size():
				return { }
			payloads.append(values)
		var fp_count := r.get_aligned_u8()
		var post_fps := PackedInt32Array()
		var e_digests := PackedInt32Array()
		for i in fp_count:
			post_fps.append(_to_signed(r.get_aligned_u32()))
			e_digests.append(_to_signed(r.get_aligned_u32()))
		return {
			"epoch": epoch,
			"ack_of_acks": ack_of_acks,
			"transitions": transitions,
			"payloads": payloads,
			"post_fps": post_fps,
			"e_digests": e_digests,
		}


	# Writes one ACK frame from parallel columns beginning at [param base]. The
	# environment digest rides beside the two fingerprints because a divergence
	# the owner cannot charge to the command or to the environment is charged to
	# the simulation, and that last step is only honest once the first two have
	# been tested against authority's own values rather than assumed.
	static func encode_ack(
			base: int,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			flags: PackedByteArray,
	) -> PackedByteArray:
		var w := NetwBitBuffer.Writer.new()
		var count := mini(
			mini(c_hashes.size(), e_digests.size()),
			mini(post_fps.size(), mini(flags.size(), _MAX_RUN)),
		)
		NetwCodec.put_varint(w, base)
		w.put_aligned_u8(count)
		for i in count:
			w.put_aligned_u32(c_hashes[i] & _U32)
			w.put_aligned_u32(e_digests[i] & _U32)
			w.put_aligned_u32(post_fps[i] & _U32)
			w.put_aligned_u8(flags[i])
		return w.to_bytes()


	# Reads one ACK frame into {base, c_hashes, e_digests, post_fps, flags} as
	# parallel packed columns, the one hot-path crossing this family never turns
	# into dictionaries.
	static func decode_ack(payload: PackedByteArray) -> Dictionary:
		if payload.is_empty():
			return { }
		var r := NetwBitBuffer.Reader.new(payload)
		var base := NetwCodec.get_safe_varint(r)
		if base < 0:
			return { }
		var count := r.get_aligned_u8()
		var c_hashes := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var flags := PackedByteArray()
		for i in count:
			c_hashes.append(_to_signed(r.get_aligned_u32()))
			e_digests.append(_to_signed(r.get_aligned_u32()))
			post_fps.append(_to_signed(r.get_aligned_u32()))
			flags.append(r.get_aligned_u8())
		return {
			"base": base,
			"c_hashes": c_hashes,
			"e_digests": e_digests,
			"post_fps": post_fps,
			"flags": flags,
		}


	# Fingerprints cross as u32 and are held as signed 32-bit, the width the journal
	# column carries, so a value never changes representation between the two.
	static func _to_signed(value: int) -> int:
		return value - 0x100000000 if value >= 0x80000000 else value


	static func _encode_zigzag(value: int) -> int:
		return (value << 1) if value >= 0 else ((-value << 1) - 1)


	static func _decode_zigzag(value: int) -> int:
		return (value >> 1) if value & 1 == 0 else -((value >> 1) + 1)


class _PredictionEngine extends RefCounted:
	const TAPE_HISTORY_LIMIT := 256
	# Acknowledgement records repeated per send. The lane is unreliable, so it
	# re-sends what the owner has not confirmed, but an owner whose confirmation
	# never arrives must not grow the frame without bound. Its own
	# acknowledgement floor advances the moment any frame lands.
	const ACK_WINDOW_MAX := 64
	# Consecutive non-shrinking recoveries that promote the next one to a full
	# closure. Fixed rather than configurable: convergence is a guarantee the
	# kernel owes at every configuration, not a knob whose wrong value can
	# forfeit it.
	const ESCALATE_NONSHRINK := 3

	var _iface_ref: WeakRef
	var _entity: NetwEntity
	var _handle: PredictionHandle
	var _role: PredictionHandle.Role = PredictionHandle.Role.REMOTE
	var _correction: PredictionHandle.CorrectionMode = \
			PredictionHandle.CorrectionMode.REPLAY
	var _state_binding: NetwSyncSetBinding
	var _input_binding: NetwSyncSetBinding
	var _timeline: NetwTimeline
	# The step a replay re-runs at, adopted from whatever timing the last pass
	# carried. The engine holds no clock of its own so a kernel cannot find one.
	var _tick_delta: float = 1.0 / 60.0
	var _registered: bool = false

	# Field key -> velocity field key, the derivative pairs an EXTRAPOLATED snap
	# restore projects forward. Built once per rewire from the state set specs,
	# empty when no field declares a project_channel present in the set.
	var _restore_projection: Dictionary[StringName, StringName] = { }
	# Per-field fractions a recovery steps toward the rebase rather than writing
	# it outright, read off each property's own declaration at wire time.
	var _converge_rules: Dictionary[StringName, float] = { }
	# Fields a sub-teleport in-domain recovery leaves on the predicted body,
	# read off each property's teleport_only() mark at wire time.
	var _withheld_fields: Dictionary[StringName, bool] = { }
	# Fields whose own divergence never triggers a correction, read off each
	# property's reconcile_only() mark at wire time.
	var _trigger_excludes: Dictionary[StringName, bool] = { }
	# Per-field divergence thresholds overriding the entity default, read off
	# each property's epsilon() mark at wire time.
	var _epsilon_overrides: Dictionary[StringName, float] = { }

	# Fields whose NetwInterpolate spec is ANGLE mode, built alongside the
	# projection map. Their compare, blend delta, and pose delta use the shortest
	# arc so a wrap crossing never reads as a near-full turn.
	var _angle_fields: Dictionary[StringName, bool] = { }

	# Whether the correction being applied crossed the teleport tier, latched for
	# the recovered emission.
	var _last_correction_teleported: bool = false

	# The config hash the property-class report last judged, so it fires once per
	# distinct config across the rewires a role change triggers.
	var _validated_class_hash: int = 0

	# Whether this stream has seen its gain-edge full row yet. A masked stream
	# opens with a whole row, and every later frame merges its changed fields over
	# the last, so past the gain edge the merged row is the sender's coherent row
	# for that frame's tick and a correction may consume it. Before the gain edge
	# the merged row is a partial mosaic authoritative at no single tick, so
	# corrections wait. An unmasked set reports a whole row on every frame and so
	# arms on its first, leaving the gate transparent to every non-masked recipe.
	var _stream_reconstructed: bool = false

	# Predict cursor.
	var _latest_input_tick: int = -1
	var _last_driven_input_tick: int = -1
	var _last_recorded_input_tick: int = -1
	var _frame_input: Dictionary = { }
	var _stall_input: Dictionary = { }
	# FRAME tape authoring and post-solve entry-keyed prediction history.
	var _tape_epoch: int = 0
	var _next_tape_entry_index: int = 0
	var _last_driven_entry_index: int = -1
	var _last_recorded_entry_index: int = -1
	var _authored_tape: Array[Dictionary] = []
	var _entry_history := NetwTimeline.new(TAPE_HISTORY_LIMIT)
	# What this engine can prove about the transitions it drove. Written only
	# here, read by the handle, the overlay, and every later verification.
	var _journal := NetwPredictJournal.new(TAPE_HISTORY_LIMIT)
	# Owner lane, authority side: transition -> {label, fresh, command}. The
	# command rides with its transition, so a queued entry is never missing the
	# input it needs.
	var _command_queue: Dictionary = { }
	var _command_epoch: int = -1
	# Authority lane, owner side: the highest transition authority has
	# acknowledged, which floors both lanes' redundancy windows.
	var _ack_of_acks: int = -1
	# Authority lane, authority side: the highest transition the owner has
	# confirmed receiving an acknowledgement for, which trims the re-send run.
	var _owner_ack_floor: int = -1
	# Entry-indexed replay cursor over the owner lane's queue.
	var _replay_cursor: int = -1
	var _replay_warmed: bool = false
	# The TICK tier's standing-buffer latch, the counterpart of _replay_warmed.
	var _consume_warmed: bool = false
	var _last_replayed_label: int = -1
	var _last_replayed_fresh: bool = false
	# Consume cursors.
	var _next_input_tick: int = -1
	var _ack: int = -1
	var _last_input: Dictionary = { }
	# Whether the last consume step moved the ack, which decides both whether the
	# state stream authors a frame this tick and which timeline slot the recorder
	# writes.
	var _ack_advanced: bool = false

	# Pauses non-teleport recoveries after a contact, so a settling transient is
	# not corrected through.
	var _cooldown_until_tick: int = -1

	# Recovery-convergence evidence, fed to escalation_after: consecutive
	# recoveries whose divergence did not shrink, the dominant-axis sign and
	# magnitude of the last one (magnitude negative while there is none), and
	# the promotion the next staged recovery consumes.
	var _nonshrink_streak: int = 0
	var _last_recovery_sign: int = 0
	var _last_recovery_divergence: float = -1.0
	var _escalate_next: bool = false

	# Declared-island state. _out_of_domain_until is the label an open window
	# stops covering, or -1 when none is open; a window opens on a fact that makes
	# some antecedent unequal and stays open a cooldown past it, because a contact
	# keeps perturbing the bodies after the frame that reported it. _e_digest is
	# the fingerprint of the declared world facts the last drive ran against, and
	# _island_epoch is the world version the game last told us about.
	var _out_of_domain_until: int = -1
	var _e_digest: int = 0
	var _island_epoch: int = -1
	var _island_gap_reported: bool = false
	# The declared sensors' values from the newest pre-drive sample, the ones
	# the current digest was folded over. The drive reads these back so the
	# transition and its digest describe the same world.
	# TODO: carry samples per tape entry once a replaying tier declares
	# sensors, so a re-run transition reads the world its original drive read.
	var _sensor_samples: Dictionary = { }

	# What the owner claims it reached, keyed by transition, held on authority
	# until authority has run that transition itself. The owner ships a claim
	# ahead of the consume cursor, so a verdict taken on arrival would judge a row
	# authority has not written yet.
	#   { transition: int -> { post_fp: int, e_digest: int } }
	var _owner_claims: Dictionary = { }


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
			_input_binding.volatile_external = false
		if _entity:
			if _entity.control_changed.is_connected(_on_control_changed):
				_entity.control_changed.disconnect(_on_control_changed)
			if _entity.reparented.is_connected(_on_reparented):
				_entity.reparented.disconnect(_on_reparented)


	func handle() -> PredictionHandle:
		return _handle


	func network_tick(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		if _handle.schedule == PredictionHandle.Schedule.TICK:
			simulate_tick(timing)
			return
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_author_tick(timing.tick)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_author_tick(timing.tick)


	func simulate_tick(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_step(timing.delta, timing.tick)
			PredictionHandle.Role.CONSUME:
				_consume_step(timing.delta, timing.tick)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_step(timing.delta, timing.tick)


	func simulate_frame(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		if _handle.schedule != PredictionHandle.Schedule.FRAME:
			return
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_frame_step(timing)
			PredictionHandle.Role.CONSUME:
				_consume_frame_step(timing)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_frame_step(timing)


	# Keeps the step a receive-driven path replays at. A correction arrives on no
	# pump, so it has no timing of its own and reuses the last one a pass carried.
	func _adopt_timing(timing: PredictTiming) -> void:
		if timing.ticktime > 0.0:
			_tick_delta = timing.ticktime

#region Kernels

	# The decision half of every drive, compare, and recovery the engine performs,
	# fenced away from the half that writes the result.
	#
	# Each kernel is static on purpose. A static function has no self, so it can
	# reach no clock, no node, no binding, and no engine field: its answer is a
	# function of its arguments and nothing else, which is exactly the property
	# that lets a transition be reproduced from its journal row alone. The
	# language enforces that from the inside; tests/unit/sim/
	# test_predict_kernel_purity.gd enforces it from the outside, on the tokens.


	## Which of the three things a FRAME consume pass can do this frame.
	enum ConsumeAction {
		## A queued transition is available past the standing buffer, so it runs.
		REPLAY,
		## Transitions are queued but not yet past the buffer, so the pass waits.
		HOLD,
		## Nothing is queued at all, so the pass has no command to run.
		STARVED,
	}


	## Chooses the one drive a FRAME pass applies, returning
	## [code]{ label: int, fresh: bool, kind: DriveKind }[/code].
	##
	## A frame with no newer input than the last one driven repeats that input
	## under the label it already had rather than inventing one, so a peer's
	## transition count follows its frames and never its ticks.
	## [codeblock]
	## latest > driven  ──> FRESH,  labeled by the input
	## latest <= driven ──> REPEAT, labeled by the input it repeats
	## latest < 0       ──> REPEAT, labeled by the pass timing
	## [/codeblock]
	static func predict_fold(
			latest_input_tick: int,
			last_driven_input_tick: int,
			frame_tick: int,
	) -> Dictionary:
		var fresh := latest_input_tick > last_driven_input_tick
		return {
			&"label": latest_input_tick if latest_input_tick >= 0 else frame_tick,
			&"fresh": fresh,
			&"kind": PredictionHandle.DriveKind.FRESH if fresh \
					else PredictionHandle.DriveKind.REPEAT,
		}


	## Chooses whether authority replays a queued transition, waits for one that
	## has not arrived, or runs dry, returning
	## [code]{ action: ConsumeAction, warmed: bool }[/code].
	##
	## Authority keeps a standing buffer of queued transitions so one late arrival
	## costs one held frame rather than a stall, which is why the verdict reads
	## [param depth] against [param buffer] instead of against zero.
	##
	## The warm latch cannot currently change the verdict: it is set on exactly
	## the test the [constant ConsumeAction.REPLAY] branch then re-applies, so
	## replay happens if and only if depth exceeds the buffer, warmed or not.
	# TODO: decide whether a warmed cursor should keep replaying at any positive
	# depth, once the tick and frame tiers share one standing-buffer consume. A
	# latch that never changes an answer should then either earn one or go.
	static func consume_plan(depth: int, buffer: int, warmed: bool) -> Dictionary:
		var now_warmed := warmed or depth > buffer
		var action := ConsumeAction.STARVED
		if now_warmed and depth > buffer:
			action = ConsumeAction.REPLAY
		elif depth > 0:
			action = ConsumeAction.HOLD
		return { &"action": action, &"warmed": now_warmed }


	## Whether an exact comparison reached a verdict for a transition.
	enum ExactVerdict {
		## Nobody has compared fingerprints for this transition yet.
		UNJUDGED,
		## The two independently recorded fingerprints are equal.
		EQUAL,
		## The two independently recorded fingerprints differ.
		UNEQUAL,
	}


	## Judges one acknowledged transition against the owner's prediction of it,
	## returning
	## [code]{ divergence: float, corrected: bool, in_domain: bool }[/code].
	##
	## Which comparison runs is the transition's own
	## [enum NetwPredictJournal.Domain], not a setting. A transition whose
	## antecedents were all declared equal has no tolerance to spend, so any
	## inequality of [param verdict] is a divergence. One whose antecedents were
	## not is compared by tolerance instead, since the peers never claimed the
	## exactness a fingerprint would test for.
	## [codeblock]
	## IN_DOMAIN  + UNEQUAL  ──> corrected, no epsilon consulted
	## IN_DOMAIN  + EQUAL    ──> agreed,    no epsilon consulted
	## IN_DOMAIN  + UNJUDGED ──> epsilon, because blind is not exact
	## OUT_OF_DOMAIN         ──> epsilon
	## [/codeblock]
	## An empty [param predicted] means nothing was recorded at or before the
	## acknowledgement, so there is no transition to judge. That reports
	## [code]INF[/code] and corrects, because an unjudged transition must never
	## pass as an agreeing one.
	##
	## [param field_sink] receives the per-property error and is an argument
	## rather than a return so
	## [member NetwLagCompensationInterface.PredictionHandle.last_field_divergence]
	## keeps its identity for anyone already holding it. It is filled on every
	## branch, because the magnitude of a divergence is worth reporting even where
	## it no longer decides one. An unjudged transition leaves it untouched, since
	## reporting a previous transition's errors as this one's would be worse than
	## reporting none.
	static func evaluate(
			domain: NetwPredictJournal.Domain,
			verdict: ExactVerdict,
			predicted: Dictionary,
			payload: Dictionary,
			epsilon: float,
			overrides: Dictionary,
			excludes: Dictionary,
			angles: Dictionary,
			field_sink: Dictionary,
	) -> Dictionary:
		var in_domain := domain == NetwPredictJournal.Domain.IN_DOMAIN
		if predicted.is_empty():
			return {
				&"divergence": INF,
				&"corrected": true,
				&"in_domain": in_domain,
			}
		var divergence := PredictionHandle.divergence_by_field(
			predicted,
			payload,
			angles,
			field_sink,
		)
		var exact := in_domain and verdict != ExactVerdict.UNJUDGED
		return {
			&"divergence": divergence,
			&"corrected": verdict == ExactVerdict.UNEQUAL if exact \
					else PredictionHandle.diverged(
						predicted,
						payload,
						epsilon,
						overrides,
						excludes,
						angles,
					),
			&"in_domain": in_domain,
		}


	## Charges one divergence to the antecedent that differed, returning the
	## [enum NetwPredictJournal.Attribution] it earns.
	##
	## The recurrence has three antecedents and they are tested in the order
	## their evidence is strongest. The command is compared hash to hash, the
	## environment digest to digest, and only a divergence that survives both
	## reaches the simulation, which is why
	## [constant NetwPredictJournal.Attribution.SIMULATION] means a bug with an
	## address rather than a leftover.
	static func attribute(
			command_equal: bool,
			environment_equal: bool,
	) -> NetwPredictJournal.Attribution:
		if not command_equal:
			return NetwPredictJournal.Attribution.COMMAND
		if not environment_equal:
			return NetwPredictJournal.Attribution.ENVIRONMENT
		return NetwPredictJournal.Attribution.SIMULATION


	## Stages the one write a correction applies, returning
	## [code]{ restore: Dictionary, write: Dictionary, teleport: bool,
	## skip: bool }[/code].
	##
	## The whole recovery is decided here and applied by the shell in a single
	## write, so a correction has no tail: nothing is left outstanding to be eased
	## in over later frames, and the recorded state after a recovery is exactly
	## what was staged. [code]restore[/code] is the payload to apply,
	## [code]write[/code] is what the display is told moved, and
	## [code]skip[/code] means this recovery declines to write at all.
	## [codeblock]
	## OBSERVE                 ──> skip (the divergence was reported, and
	##                             reporting it was the whole policy)
	## REPLAY                  ──> restore all, write nothing (replay
	##                             reaches the present under its own power)
	## SNAP, pose >= threshold ──> restore all, teleport
	## SNAP, suppressed        ──> skip
	## SNAP, out of domain     ──> restore all (partiality is an in-domain
	##                             refinement)
	## SNAP, below threshold   ──> restore all but the withheld fields
	## [/codeblock]
	## [param policy] is what the entity asked for and [param correction] is the
	## mechanism it resolved to, which is why both are passed: every policy but
	## [constant PredictionHandle.RecoveryPolicy.OBSERVE] is carried out by its
	## mechanism, and OBSERVE is the one that declines to have one.
	## The teleport tier is measured by [param pose_error] against
	## [param teleport_threshold]. An entity that declares no derivative channel
	## has no pose to measure, and the shell reports [code]INF[/code] for it
	## rather than zero: a recovery that cannot measure a pose error is not
	## entitled to claim it is below the threshold, so it restores everything.
	##
	## [param withheld] names the fields a sub-teleport recovery leaves on the
	## predicted body. A contractive field that re-converges on its own is worse
	## off rewound to the acknowledged tick than left alone, which is what
	## [method NetwScriptModel.PropertyConfig.teleport_only] declares. [param converge_rules] is the middle answer between withholding a
	## field and writing it outright, applied through [method converge_toward].
	## A teleport ignores both declarations, because past the threshold the
	## predicted body has nothing worth keeping.
	##
	## Under
	## [constant NetwLagCompensationInterface.PredictionHandle.RestoreMode.EXTRAPOLATED]
	## the derivative-declaring properties carry forward to now, so a solver body
	## lands near where it is rather than at the stale acknowledgement. The span
	## is capped by [param max_restore_ticks] because a backlogged acknowledgement
	## would otherwise launch the body along a long straight line off a curved
	## path.
	## [br][br]
	## [param domain], [param attribution], and [param contact_window] key the
	## partial write on what the divergence is. Withholding a field or stepping
	## partway toward authority is sound only where the antecedents were declared
	## reproducible, so a divergence labeled
	## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN], or one still
	## [constant NetwPredictJournal.Attribution.UNATTRIBUTED] while an open
	## contact window is disturbing the bodies, restores the whole closure
	## instead: the recovery cannot know which fields are safe to leave behind,
	## so it leaves none.
	static func recover(
			payload: Dictionary,
			policy: PredictionHandle.RecoveryPolicy,
			correction: PredictionHandle.CorrectionMode,
			snap_restore: PredictionHandle.RestoreMode,
			projection: Dictionary,
			withheld: Dictionary,
			converge_rules: Dictionary,
			current: Dictionary,
			angles: Dictionary,
			pose_error: float,
			teleport_threshold: float,
			suppressed: bool,
			ack_age_ticks: int,
			max_restore_ticks: int,
			tick_delta: float,
			domain: NetwPredictJournal.Domain = NetwPredictJournal.Domain.IN_DOMAIN,
			attribution: NetwPredictJournal.Attribution = \
					NetwPredictJournal.Attribution.UNATTRIBUTED,
			contact_window: bool = false,
	) -> Dictionary:
		# The divergence has already been reported by the time a recovery is
		# staged, so a policy that only reports has nothing left to do. Declining
		# here rather than in the shell keeps the whole decision in one place: no
		# caller has to know that one policy writes nothing.
		if policy == PredictionHandle.RecoveryPolicy.OBSERVE:
			return {
				&"restore": { },
				&"write": { },
				&"teleport": false,
				&"skip": true,
			}
		if correction == PredictionHandle.CorrectionMode.REPLAY:
			return {
				&"restore": payload,
				&"write": { },
				&"teleport": false,
				&"skip": false,
			}
		var projected := snap_restore == PredictionHandle.RestoreMode.EXTRAPOLATED
		var restore := payload
		if projected and not projection.is_empty():
			var span := clampi(ack_age_ticks, 0, max_restore_ticks)
			restore = project_payload(payload, projection, float(span) * tick_delta)
		if pose_error >= teleport_threshold:
			return {
				&"restore": restore,
				&"write": restore,
				&"teleport": true,
				&"skip": false,
			}
		# A settling body and a diverging one look alike for a few ticks after a
		# contact, and correcting through that transient fights the solver rather
		# than the divergence. The recovery declines instead of writing a value it
		# would have to take back.
		if suppressed:
			return {
				&"restore": { },
				&"write": { },
				&"teleport": false,
				&"skip": true,
			}
		# Partiality is an in-domain refinement. A divergence outside the
		# declared domain, or one nobody could charge while a contact was still
		# disturbing the bodies, gives no ground to decide which fields are safe
		# to leave predicted, so the recovery re-bases the whole closure.
		if domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN \
				or (attribution == NetwPredictJournal.Attribution.UNATTRIBUTED
						and contact_window):
			return {
				&"restore": restore,
				&"write": restore,
				&"teleport": false,
				&"skip": false,
			}
		if not withheld.is_empty():
			restore = restore.duplicate()
			for field: StringName in withheld:
				restore.erase(field)
		restore = converge_toward(restore, current, converge_rules, angles)
		return {
			&"restore": restore,
			&"write": restore,
			&"teleport": false,
			&"skip": false,
		}


	## Decides whether the next staged recovery is promoted to a full closure,
	## returning [code]{ streak: int, sign: int, escalate: bool }[/code].
	##
	## A recovery answers a divergence, so a run of recoveries whose divergence
	## never shrinks is not converging, and two consecutive deltas that point
	## opposite ways are overshooting each other. Either way the partial writes
	## have proven they cannot close this divergence, so the next recovery is
	## promoted to the full closure a teleport applies, without the pose-error
	## precondition. This is what bounds every recovery sequence: it shrinks, or
	## it escalates, and no reachable configuration holds an oscillation open.
	## [codeblock]
	## divergence shrank         ──> counters reset, no escalation
	## 3 non-shrinking in a row  ──> escalate the next recovery
	## two opposing-sign deltas  ──> escalate the next recovery
	## [/codeblock]
	## [param streak] and [param last_sign] are the caller's counters from the
	## previous verdict, and [param last_divergence] is the magnitude the
	## previous recovery answered, negative while there is none to compare
	## against. A first recovery has shown no shrink yet, so it counts toward
	## the streak rather than resetting it.
	static func escalation_after(
			streak: int,
			last_sign: int,
			last_divergence: float,
			divergence: float,
			sign: int,
	) -> Dictionary:
		if last_divergence >= 0.0 and divergence < last_divergence:
			return { &"streak": 0, &"sign": 0, &"escalate": false }
		var grown := streak + 1
		var flipped := sign != 0 and last_sign != 0 and sign != last_sign
		var escalate := grown >= ESCALATE_NONSHRINK or flipped
		return {
			&"streak": 0 if escalate else grown,
			&"sign": sign,
			&"escalate": escalate,
		}


	## The sign of a delta's dominant axis: [code]-1[/code], [code]0[/code], or
	## [code]1[/code].
	##
	## Two recoveries agree or disagree in direction along the axis where their
	## delta is largest, so the sign is read there rather than folded across
	## axes where a small orthogonal drift could mask a flip. A type with no
	## signed axis reports [code]0[/code], which never counts as a flip.
	static func delta_sign(delta: Variant) -> int:
		match typeof(delta):
			TYPE_FLOAT:
				return int(signf(delta as float))
			TYPE_VECTOR2:
				var v2 := delta as Vector2
				return int(signf(v2.x if absf(v2.x) >= absf(v2.y) else v2.y))
			TYPE_VECTOR3:
				var v3 := delta as Vector3
				var dominant := v3.x
				if absf(v3.y) > absf(dominant):
					dominant = v3.y
				if absf(v3.z) > absf(dominant):
					dominant = v3.z
				return int(signf(dominant))
		return 0


	## Drops from [param projection] every field whose derivative channel is
	## itself diverged, returning the pairs a restore may still project along.
	##
	## Projecting a field forward along a channel authority disagrees about
	## launches the restore along the wrong line, which widens the divergence
	## the recovery answers. The guard is per field: one diverged channel costs
	## its own field the projection and no other, so a converged channel keeps
	## the landing accuracy projection buys. A channel is diverged when its
	## entry in [param field_divergence] reaches its own threshold, read from
	## [param overrides] with [param epsilon] as the fallback.
	static func guard_projection(
			projection: Dictionary,
			field_divergence: Dictionary,
			epsilon: float,
			overrides: Dictionary,
	) -> Dictionary:
		if projection.is_empty() or field_divergence.is_empty():
			return projection
		var out := { }
		for field: StringName in projection:
			var channel: StringName = projection[field]
			var limit := float(overrides.get(channel, epsilon))
			if float(field_divergence.get(channel, 0.0)) < limit:
				out[field] = channel
		return out


	## Decides whether one transition is entitled to reproduce exactly, returning
	## the [enum NetwPredictJournal.Domain] it earns.
	##
	## Exactness is claimed, never assumed. An entity that never declared an
	## island through
	## [method NetwLagCompensationInterface.PredictionHandle.configure_island]
	## has said nothing about whether its antecedents match its authority's, so
	## [param declared] false labels every one of its transitions out of domain
	## and it is compared by tolerance exactly as it always was. An island
	## declared [param approximate] is out of domain for the opposite reason: the
	## declaration says so.
	##
	## For a declared island the only remaining question is whether the transition
	## ran inside an open out-of-domain window, which is what makes the label a
	## property of when the drive happened rather than of when anyone judged it.
	static func domain_of(
			declared: bool,
			approximate: bool,
			label: int,
			window_until: int,
	) -> NetwPredictJournal.Domain:
		if not declared or approximate \
				or (window_until >= 0 and label < window_until):
			return NetwPredictJournal.Domain.OUT_OF_DOMAIN
		return NetwPredictJournal.Domain.IN_DOMAIN


	## Extends an out-of-domain window to cover [param cooldown] labels past the
	## fact that opened it, returning the label the window stops covering.
	##
	## The disturbing transition is itself covered, not just the ones after it.
	## Windows merge rather than restart, so a second fact arriving inside an open
	## window can only lengthen the coverage: a nearer disturbance does not undo
	## the one already being covered, so it must never cut that window short.
	static func window_after(label: int, cooldown: int, window_until: int) -> int:
		return maxi(window_until, label + maxi(0, cooldown) + 1)


	## Folds the declared world facts into one fingerprint, so two peers can
	## discover they ran against different environments without either shipping
	## its environment.
	##
	## The fold walks the sorted sensor names because [Dictionary] iteration order
	## is not guaranteed to agree between peers, and a digest that disagreed for
	## that reason would accuse the environment every time it was read.
	static func environment_digest(epoch: int, samples: Dictionary) -> int:
		var names := samples.keys()
		names.sort()
		var bytes := PackedByteArray()
		bytes.append_array(var_to_bytes(epoch))
		for name: StringName in names:
			bytes.append_array(var_to_bytes(name))
			bytes.append_array(var_to_bytes(samples[name]))
		return NetwPredictJournal.fnv1a(bytes)


	# Error delta target - current for the linearly-steppable pose types, shortest
	# arc for an angle field so a wrap crossing never steps the long way round. A
	# type with no meaningful midpoint (a quaternion, whose error is angular)
	# returns null, and every caller restores it outright instead.
	static func _pose_delta(
			target: Variant,
			current: Variant,
			is_angle: bool = false,
	) -> Variant:
		match typeof(target):
			TYPE_FLOAT:
				if is_angle:
					return angle_difference(current as float, target as float)
				return (target as float) - (current as float)
			TYPE_VECTOR2:
				return (target as Vector2) - (current as Vector2)
			TYPE_VECTOR3:
				return (target as Vector3) - (current as Vector3)
		return null


	static func _pose_scale(value: Variant, factor: float) -> Variant:
		match typeof(value):
			TYPE_FLOAT:
				return (value as float) * factor
			TYPE_VECTOR2:
				return (value as Vector2) * factor
			TYPE_VECTOR3:
				return (value as Vector3) * factor
		return value


	static func _pose_sum(a: Variant, b: Variant) -> Variant:
		match typeof(a):
			TYPE_FLOAT:
				return (a as float) + (b as float)
			TYPE_VECTOR2:
				return (a as Vector2) + (b as Vector2)
			TYPE_VECTOR3:
				return (a as Vector3) + (b as Vector3)
		return a


	## Replaces each converging property in [param restore] with a bounded step
	## from [param current] toward it, returning the staged payload.
	##
	## A field that drives the simulation but has no derivative to project along
	## cannot be restored outright: the acknowledged value is already wrong by the
	## time it lands, so writing it forks the body again and the fork outlives
	## every recovery. Stepping a declared fraction of the way closes the same
	## error without ever writing a value the body was not near.
	##
	## The step is taken as part of the recovery and is complete when the recovery
	## is. Nothing is left outstanding, so a converging field never becomes a
	## background write that keeps moving the body between recoveries.
	## [codeblock]
	## rate 1.0  ──> restores outright, the declaration opting out
	## rate 0.25 ──> current + a quarter of the error
	## rate 0.0  ──> no rule, restores outright
	## [/codeblock]
	## A type with no meaningful midpoint (a bool, a [Quaternion]) restores
	## outright rather than being dropped, because a rebase that silently omitted
	## a property would leave the body holding a predicted value it was never
	## entitled to keep.
	static func converge_toward(
			restore: Dictionary,
			current: Dictionary,
			rules: Dictionary,
			angles: Dictionary,
	) -> Dictionary:
		if rules.is_empty():
			return restore
		var out := restore.duplicate()
		for field: StringName in rules:
			if not restore.has(field) or not current.has(field):
				continue
			var rate := clampf(rules[field], 0.0, 1.0)
			if rate <= 0.0 or rate >= 1.0:
				continue
			var delta: Variant = _pose_delta(
				restore[field],
				current[field],
				angles.has(field),
			)
			if delta == null:
				continue
			out[field] = _pose_sum(
				current[field],
				_pose_scale(delta, rate),
			)
		return out


	## Projects every property that declares a derivative channel forward by
	## [param age].
	##
	## A property missing its velocity in [param payload], or carrying a type
	## [NetwProject] cannot project, restores verbatim rather than being dropped:
	## a rebase that silently omitted a property would leave the body holding a
	## predicted value it was never entitled to keep.
	static func project_payload(
			payload: Dictionary,
			projection: Dictionary,
			age: float,
	) -> Dictionary:
		if age <= 0.0:
			return payload
		var out := payload.duplicate()
		for field: StringName in projection:
			var velocity_key: StringName = projection[field]
			if not payload.has(field) or not payload.has(velocity_key):
				continue
			var value: Variant = payload[field]
			if not NetwProject.supports(typeof(value)):
				continue
			out[field] = NetwProject.project(value, payload[velocity_key], age)
		return out

#endregion


	func finalize_frame_state() -> void:
		if _handle.schedule != PredictionHandle.Schedule.FRAME:
			return
		if _role != PredictionHandle.Role.PREDICT:
			return
		var state := _capture()
		if _last_driven_entry_index > _last_recorded_entry_index:
			_entry_history.record_state(_last_driven_entry_index + 1, state)
			_journal.close(
				_last_driven_entry_index,
				_state_fingerprint(state),
			)
			_last_recorded_entry_index = _last_driven_entry_index
		if _last_driven_input_tick > _last_recorded_input_tick:
			_timeline.record_state(_last_driven_input_tick + 1, state)
			_last_recorded_input_tick = _last_driven_input_tick


	func uses_schedule(schedule: PredictionHandle.Schedule) -> bool:
		return _handle.schedule == schedule


	func tape_transitions() -> Array[Dictionary]:
		if _role == PredictionHandle.Role.PREDICT:
			return _authored_tape.duplicate(true)
		var out: Array[Dictionary] = []
		var indices: Array = _command_queue.keys()
		indices.sort()
		for index: int in indices:
			var queued := _command_queue[index] as Dictionary
			out.append({
				"index": index,
				"label": int(queued.get("label", -1)),
				"fresh": bool(queued.get("fresh", false)),
			})
		return out


	func transition_state_at(entry_index: int) -> Dictionary:
		# A TICK transition is its tick, so its post-state lives in the predicted
		# timeline rather than in the FRAME tier's entry history.
		if _handle.schedule == PredictionHandle.Schedule.TICK:
			return _timeline.state_at(entry_index + 1) if _timeline else { }
		return _entry_history.state_at(entry_index + 1)


	func journal() -> NetwPredictJournal:
		return _journal


	# --- Owner and authority lanes ---


	# Ships the transitions still in flight with the command that drove each
	# fresh one, floored by what authority has acknowledged. Every send repeats
	# the whole unacknowledged range, so a lost datagram heals on the next frame
	# rather than on a retransmit.
	func _send_command_frame() -> void:
		var route := _route()
		if route < 0:
			return
		_send_lane(
			1,
			route,
			NetwFrameEnvelope.Channel.PREDICT_COMMAND,
			build_command_frame(),
		)


	# The frame the owner lane would ship this step, or empty when there is
	# nothing outstanding. Split from the send so the bytes are testable and so
	# the kernel half stays a pure function of the tape and the ack floor.
	func build_command_frame() -> PackedByteArray:
		if _role != PredictionHandle.Role.PREDICT or _authored_tape.is_empty():
			return PackedByteArray()
		var window := maxi(1, _input_binding.set.window + 2)
		var rows: Array = []
		var payloads: Array = []
		var codecs := _input_codecs()
		var quantizers: Array = codecs[0]
		var types: Array = codecs[1]
		var oldest := maxi(
			_ack_of_acks + 1,
			int(_authored_tape.back().get("index", 0)) - window + 1,
		)
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index < oldest:
				continue
			rows.append(entry)
			if not bool(entry.get("fresh", false)):
				continue
			var input := _timeline.input_at(int(entry.get("label", -1)))
			var values: Array = []
			for key: StringName in codecs[2]:
				values.append(input.get(key, _stall_input.get(key)))
			payloads.append(values)
		if rows.is_empty():
			return PackedByteArray()
		# The owner claims a fingerprint only for what it has finished. A row still
		# open holds a zero authority would read as a claim of zero, so the section
		# covers the closed prefix of the window and stops at the first row the
		# owner cannot yet speak for.
		var post_fps := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var closed := _journal.last_closed()
		for entry: Dictionary in rows:
			var index := int(entry.get("index", -1))
			if index > closed:
				break
			var row := _journal.row_at(index)
			if row.is_empty():
				break
			post_fps.append(int(row.get(&"post_fp", 0)))
			e_digests.append(int(row.get(&"e_digest", 0)))
		return _PredictFrames.encode_command(
			_tape_epoch,
			_ack_of_acks,
			rows,
			payloads,
			quantizers,
			types,
			post_fps,
			e_digests,
		)


	# Ships what authority actually ran for each transition it replayed, floored
	# by what the owner has confirmed. A frame that drove nothing acknowledges
	# nothing, so the lane never claims a transition authority held.
	func _send_ack_frame() -> void:
		var route := _route()
		if route < 0 or not _entity:
			return
		_send_lane(
			_entity.controller,
			route,
			NetwFrameEnvelope.Channel.PREDICT_ACK,
			build_ack_frame(),
		)


	# The frame the authority lane would ship this step, or empty when authority
	# has acknowledged nothing outstanding.
	func build_ack_frame() -> PackedByteArray:
		if _role != PredictionHandle.Role.CONSUME or _ack < 0:
			return PackedByteArray()
		# Authority acknowledges a transition only once its drive has produced the
		# state, because the fingerprint is the acknowledgement. An open row would
		# ship a zero the owner would read as a divergence against every one of
		# its own correctly predicted states.
		var frontier := mini(_ack, _journal.last_closed())
		if frontier < 0:
			return PackedByteArray()
		var base := maxi(_owner_ack_floor + 1, frontier - ACK_WINDOW_MAX + 1)
		if base > frontier:
			return PackedByteArray()
		# Read the columns once and walk them. Addressing the journal per
		# transition would scan the ring per lookup and allocate a row
		# dictionary per record, on every authority frame.
		var transitions := _journal.transitions()
		var all_c := _journal.c_hashes()
		var all_e := _journal.e_digests()
		var all_fp := _journal.post_fps()
		var all_flags := _journal.flags()
		var c_hashes := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var flags := PackedByteArray()
		for i in transitions.size():
			var transition := transitions[i]
			if transition < base:
				continue
			if transition > frontier:
				break
			c_hashes.append(all_c[i])
			e_digests.append(all_e[i])
			post_fps.append(all_fp[i])
			flags.append(
				_PredictFrames.ACK_SUBSTITUTED \
				if all_flags[i] & NetwPredictJournal.ROW_SUBSTITUTED else 0,
			)
		if flags.is_empty():
			return PackedByteArray()
		return _PredictFrames.encode_ack(
			base,
			c_hashes,
			e_digests,
			post_fps,
			flags,
		)


	# Decodes one owner frame on authority. A frame that lost a command is
	# dropped whole and counted, never parsed into a transition authority would
	# then have to invent a command for.
	func receive_command_frame(payload: PackedByteArray) -> void:
		if _role != PredictionHandle.Role.CONSUME:
			return
		var codecs := _input_codecs()
		var frame := _PredictFrames.decode_command(
			payload,
			codecs[0],
			codecs[1],
		)
		if frame.is_empty():
			_handle.frames_dropped_invalid += 1
			return
		_admit_command_frame(frame, codecs[2])


	# Files a decoded owner frame into the replay queue, deduplicating the
	# redundancy overlap the way the lane's re-sent windows require.
	func _admit_command_frame(frame: Dictionary, keys: Array) -> void:
		var epoch := int(frame.get("epoch", -1))
		if epoch != _command_epoch:
			_command_epoch = epoch
			_command_queue.clear()
			# Transitions are injective only within an epoch, so a claim from the
			# previous one names a transition that is about to be reused.
			_owner_claims.clear()
			_owner_ack_floor = -1
			_replay_cursor = -1
			_replay_warmed = false
			_last_replayed_label = -1
			_last_replayed_fresh = false
			_ack = -1
			_last_input = { }
		_owner_ack_floor = maxi(
			_owner_ack_floor,
			int(frame.get("ack_of_acks", -1)),
		)
		var transitions: Array = frame.get("transitions", [])
		var payloads: Array = frame.get("payloads", [])
		# The fingerprint section covers the closed prefix of the same transition
		# run, so entry i of the columns describes transition i of the run.
		var post_fps: PackedInt32Array = frame.get("post_fps", PackedInt32Array())
		var e_digests: PackedInt32Array = frame.get(
			"e_digests",
			PackedInt32Array(),
		)
		for i in mini(post_fps.size(), e_digests.size()):
			if i >= transitions.size():
				break
			var claimed := int((transitions[i] as Dictionary).get("index", -1))
			if claimed < 0:
				continue
			_owner_claims[claimed] = {
				&"post_fp": post_fps[i],
				&"e_digest": e_digests[i],
			}
		while _owner_claims.size() > TAPE_HISTORY_LIMIT:
			var claims: Array = _owner_claims.keys()
			claims.sort()
			_owner_claims.erase(claims.front())
		var fresh_seen := 0
		for row: Dictionary in transitions:
			var index := int(row.get("index", -1))
			if index < 0:
				continue
			var command: Dictionary = { }
			if bool(row.get("fresh", false)):
				if fresh_seen >= payloads.size():
					return
				var values: Array = payloads[fresh_seen]
				fresh_seen += 1
				for i in mini(keys.size(), values.size()):
					command[keys[i]] = values[i]
			if _command_queue.has(index):
				continue
			_command_queue[index] = {
				"label": int(row.get("label", -1)),
				"fresh": bool(row.get("fresh", false)),
				"command": command,
			}
			# The TICK lane feeds the same drain the input stream fed. The
			# transition is the tick, so its command records at its label and the
			# consume cursor walks it with depth measured in ticks.
			var label := int(row.get("label", -1))
			if _handle.schedule == PredictionHandle.Schedule.TICK \
					and bool(row.get("fresh", false)) and label >= 0:
				_timeline.record_input(label, command)
				if _next_input_tick < 0:
					_next_input_tick = label
		while _command_queue.size() > TAPE_HISTORY_LIMIT:
			var indices: Array = _command_queue.keys()
			indices.sort()
			_command_queue.erase(indices.front())
		if _replay_cursor < 0 and not _command_queue.is_empty():
			var open_at: Array = _command_queue.keys()
			open_at.sort()
			_replay_cursor = int(open_at.front())
		_handle.command_queue_depth = _command_queue.size()
		_refresh_tape_diagnostics()


	func receive_ack_frame(payload: PackedByteArray) -> void:
		if _role != PredictionHandle.Role.PREDICT:
			return
		var frame := _PredictFrames.decode_ack(payload)
		if frame.is_empty():
			_handle.frames_dropped_invalid += 1
			return
		_on_ack_run(
			int(frame.get("base", 0)),
			frame.get("c_hashes", PackedInt32Array()),
			frame.get("e_digests", PackedInt32Array()),
			frame.get("post_fps", PackedInt32Array()),
			frame.get("flags", PackedByteArray()),
		)


	# Consumes one acknowledgement run. Authority sends the fingerprint of the
	# state its own replay produced, so the owner compares two independently
	# recorded post-states and needs no authoritative payload to reach a verdict.
	#
	# A transition that disagrees is charged to an antecedent here, where both
	# peers' command hashes and environment digests are in hand. _on_state sees
	# only its own row and one authoritative payload, so it could name the
	# disagreement but never its cause.
	func _on_ack_run(
			base: int,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			flags: PackedByteArray,
	) -> void:
		for i in flags.size():
			var transition := base + i
			_ack_of_acks = maxi(_ack_of_acks, transition)
			var row := _journal.row_at(transition)
			if flags[i] & _PredictFrames.ACK_SUBSTITUTED:
				_journal.mark_substituted(transition)
				_handle.substituted_count += 1
			elif not row.is_empty() and i < post_fps.size():
				# The lane re-sends a run until it is trimmed, so a transition
				# already judged must not be judged again.
				if not (int(row.get(&"flags", 0)) & NetwPredictJournal.ROW_ACKED):
					var matched := int(row.get(&"post_fp", 0)) == post_fps[i]
					_record_ack_verdict(transition, matched)
					if not matched:
						_charge_divergence(transition, row, c_hashes, e_digests, i)
		_handle.ack_confirmed_transition = _ack_of_acks


	# Names the antecedent one disagreeing transition is charged to. The columns
	# are indexed rather than addressed because the caller is already walking
	# them, and a run acknowledging the whole window would otherwise rescan the
	# ring once per record.
	func _charge_divergence(
			transition: int,
			row: Dictionary,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			index: int,
	) -> void:
		# A column authority did not send cannot convict its antecedent, so a
		# short run reads as agreement there and the charge falls through to the
		# antecedent whose evidence did arrive.
		var attribution := attribute(
			index >= c_hashes.size() \
					or int(row.get(&"c_hash", 0)) == c_hashes[index],
			index >= e_digests.size() \
					or int(row.get(&"e_digest", 0)) == e_digests[index],
		)
		_handle.last_attribution = attribution
		_handle.last_attributed_transition = transition
		# The charge also lands on the row itself, so a comparison answering
		# for an older transition later still reads the charge that transition
		# earned rather than whatever this lane judged last.
		_journal.mark_attribution(transition, attribution)


	# One place counts a fingerprint verdict, so the state-frame path and the ack
	# lane can never both count the same transition.
	func _record_ack_verdict(transition: int, matched: bool) -> void:
		_journal.mark_ack(transition, matched)
		_handle.fp_verified_count += 1
		if matched:
			return
		_handle.fp_mismatch_count += 1
		if _handle.first_divergent_transition < 0:
			_handle.first_divergent_transition = transition


	# The volatile input fields in set order with their codec inputs, the one
	# shape both lanes encode and decode through.
	func _input_codecs() -> Array:
		var node := _input_binding.node()
		var keys: Array[StringName] = []
		var quantizers: Array = []
		var types: Array = []
		if not is_instance_valid(node) or not _input_binding.set:
			return [quantizers, types, keys]
		for field in _input_binding.set.fields:
			if field.lane != NetwSyncSet.Lane.VOLATILE:
				continue
			keys.append(field.key)
			quantizers.append(field.quantizer)
			types.append(
				NetwScriptModel.get_node_property_type(node, field.key),
			)
		return [quantizers, types, keys]


	# Both lanes are unreliable and batched with the tick pump, since each send
	# already repeats the range still in flight.
	func _send_lane(
			peer: int,
			route: int,
			channel: NetwFrameEnvelope.Channel,
			bytes: PackedByteArray,
	) -> void:
		if bytes.is_empty():
			return
		var iface := _iface()
		var api := iface._api() if iface else null
		if not api:
			return
		api.replication.send_to(peer, route, channel, bytes, false, 0, "", true)


	func _route() -> int:
		var iface := _iface()
		var api := iface._api() if iface else null
		var liveness := api.liveness if api else null
		return liveness.route_of(_entity) if liveness and _entity else -1


	# Closes the authority-side journal row with the state its drive produced. The
	# recorder owns the server's post-solve capture, so the close happens where
	# that capture already lands rather than duplicating it here.
	func finalize_recorded_state(payload: Dictionary) -> void:
		if _role == PredictionHandle.Role.PREDICT:
			return
		var transition := _recorded_transition()
		var fingerprint := _state_fingerprint(payload)
		_journal.close(transition, fingerprint)
		_judge_owner_claim(transition, fingerprint)


	# Judges what the owner claimed for a transition authority has just finished.
	#
	# Authority holds a timeline per peer and checks it here, which is the whole
	# of the check: nothing is pushed back, so a mismatch leaves the owner's
	# simulation exactly where it was and leaves the game to decide what a run of
	# them means.
	func _judge_owner_claim(transition: int, fingerprint: int) -> void:
		if not _owner_claims.has(transition):
			return
		var claim: Dictionary = _owner_claims[transition]
		_owner_claims.erase(transition)
		_handle.client_fp_verified_count += 1
		if int(claim[&"post_fp"]) == fingerprint:
			return
		_handle.client_mismatch_count += 1
		# Authority ran the owner's own command unless it substituted one, so the
		# command antecedent is answered by whether it had to. The environment is
		# answered by the digest the owner shipped beside its fingerprint, which is
		# the only way authority can tell a world it disagrees about from a step
		# function it disagrees about.
		var substituted := bool(
			_journal.flags_at(transition) & NetwPredictJournal.ROW_SUBSTITUTED,
		)
		var attribution := attribute(
			not substituted,
			int(claim[&"e_digest"]) == int(
				_journal.row_at(transition).get(&"e_digest", 0),
			),
		)
		var iface := _iface()
		if iface and _entity:
			iface.peer_divergence.emit(_entity.controller, transition, attribution)


	# The transition the recorder's slot belongs to. FRAME replays acknowledge the
	# entry index they replayed, every other tier drives its own label.
	func _recorded_transition() -> int:
		if _handle.schedule == PredictionHandle.Schedule.FRAME \
				and _role == PredictionHandle.Role.CONSUME:
			return _ack
		return _handle.last_drive_label


	func order_key() -> String:
		return str(_entity.entity_id) if _entity else ""


	func history_record_tick(fallback_tick: int) -> int:
		if _role == PredictionHandle.Role.CONSUME \
				and _handle.schedule == PredictionHandle.Schedule.FRAME:
			return _last_replayed_label + 1 \
			if _ack_advanced and _last_replayed_fresh else -1
		if _role == PredictionHandle.Role.CONSUME and _ack >= 0:
			# A tick that consumed nothing produced no new authoritative state, so
			# the slot keeps the snapshot the last real consume left there instead
			# of being overwritten with a body that has coasted past the ack.
			return _ack + 1 if _ack_advanced else -1
		if _handle.schedule == PredictionHandle.Schedule.FRAME:
			return _last_driven_input_tick + 1 if _ack_advanced else -1
		return fallback_tick


	# True when the last pass replayed a transition that declines a history
	# slot. A REPEAT entry re-runs its label, so the label's slot keeps the
	# fresh entry's state, but the journal row the replay opened still owes its
	# close, or the acknowledgement frontier stalls behind it forever.
	func consumed_unslotted_transition() -> bool:
		return _role == PredictionHandle.Role.CONSUME \
				and _handle.schedule == PredictionHandle.Schedule.FRAME \
				and _ack_advanced and not _last_replayed_fresh


	func has_consumed_state_tick(state_tick: int) -> bool:
		if _role != PredictionHandle.Role.CONSUME:
			return true
		if _handle.schedule == PredictionHandle.Schedule.FRAME:
			return _last_replayed_label + 1 >= state_tick
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
		input_binding.volatile_external = false
		_timeline = null
		_restore_projection = { }
		_angle_fields = { }
		_converge_rules = { }
		_cooldown_until_tick = -1
		# A rewire re-keys the transitions the evidence was gathered over, so a
		# streak cannot mean anything across it.
		_reset_recovery_trackers()
		_latest_input_tick = -1
		_last_driven_input_tick = -1
		_last_recorded_input_tick = -1
		_frame_input = { }
		_stall_input = input_binding.snapshot_payload()
		_tape_epoch = (_tape_epoch + 1) & 0xFF
		_next_tape_entry_index = 0
		_last_driven_entry_index = -1
		_last_recorded_entry_index = -1
		_authored_tape.clear()
		_entry_history = NetwTimeline.new(TAPE_HISTORY_LIMIT)
		_journal.clear(_tape_epoch)
		_stream_reconstructed = false
		_handle.fp_verified_count = 0
		_handle.fp_mismatch_count = 0
		# A rewire re-keys the transitions a window was expressed in, so an open
		# window cannot mean anything across it and is dropped rather than carried.
		_out_of_domain_until = -1
		_e_digest = 0
		_island_epoch = -1
		_sensor_samples = { }
		_command_queue.clear()
		_command_epoch = -1
		_ack_of_acks = -1
		_owner_ack_floor = -1
		_handle.command_queue_depth = 0
		_handle.ack_confirmed_transition = -1
		_handle.first_divergent_transition = -1
		_handle.last_attributed_transition = -1
		_replay_cursor = -1
		_replay_warmed = false
		_consume_warmed = false
		_last_replayed_label = -1
		_last_replayed_fresh = false
		_handle.tape_epoch = -1
		_handle.last_transition_index = -1
		_handle.tape_queue_depth = 0

		var iface := _iface()
		if iface:
			_adopt_timing(iface.frame_timing())
		if not _handle.simulate.is_valid():
			var root := _entity.owner
			if root and root.has_method(&"_network_tick"):
				_handle.simulate = Callable(root, &"_network_tick")

		_role = _resolve_axes()
		_correction = PredictionHandle.resolve_correction_mode_for(
			_entity.owner,
			_handle.correction_mode,
		)
		_error_on_retained_predicted_props(state_binding.set)
		_validate_property_classes(state_binding.set)
		match _role:
			PredictionHandle.Role.PREDICT:
				# Owning client owns a local predicted timeline. The server's
				# authoritative history lives in the registry, never here.
				_timeline = NetwTimeline.new()
				_entity.timeline = _timeline
				# The command lane is the input carrier: each sample rides with
				# the transition it drove, so the input set never pumps SYNC and
				# survives as declaration and codec.
				input_binding.volatile_external = true
				# The authoritative state reconciles against the prediction rather
				# than snapping the predicted body, so the network never overwrites it.
				state_binding.write_gate = false
				state_binding.on_applied = _on_state_frame
				_build_restore_projection(state_binding)
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


	# Reads each state field's NetwInterpolate spec and records the field-to-velocity
	# pairs an EXTRAPOLATED snap restore projects, plus which fields are ANGLE
	# channels. A field qualifies for projection when its spec names a
	# project_channel that is also a field in this set, so the velocity is
	# replicated at the same tick. Built once per rewire, so the reconcile path only
	# reads the maps.
	func _build_restore_projection(binding: NetwSyncSetBinding) -> void:
		_restore_projection = { }
		_angle_fields = { }
		_converge_rules = { }
		_withheld_fields = { }
		_trigger_excludes = { }
		_epsilon_overrides = { }
		var node := binding.node()
		if not is_instance_valid(node) or not binding.set:
			return
		var field_keys: Dictionary[StringName, bool] = { }
		for field in binding.set.fields:
			field_keys[field.key] = true
		for field in binding.set.fields:
			var spec := NetwScriptModel.get_node_property_interpolator(node, field.key)
			if not spec:
				continue
			if spec.mode == NetwInterpolate.Mode.ANGLE:
				_angle_fields[field.key] = true
			if spec.project_channel == &"":
				continue
			if spec.forecast_tail == NetwInterpolate.Tail.HOLD:
				continue
			if field_keys.has(spec.project_channel):
				_restore_projection[field.key] = spec.project_channel
		# The per-field recovery facts live on the property marks, so this is
		# where they enter the engine: one read per rewire, one source per fact.
		for field in binding.set.fields:
			var rate := binding.converge_stiffness_of(field.key)
			if rate > 0.0:
				_converge_rules[field.key] = rate
			if binding.teleport_only_of(field.key):
				_withheld_fields[field.key] = true
			if binding.reconcile_only_of(field.key):
				_trigger_excludes[field.key] = true
			var threshold := binding.epsilon_override_of(field.key)
			if threshold >= 0.0:
				_epsilon_overrides[field.key] = threshold


	# A copy of [param payload] with every derivative-declaring field advanced by
	# [param age] seconds through its replicated velocity. Fields with no pair, no
	# velocity in the payload, or an unprojectable type restore verbatim.
	func _extrapolated_payload(payload: Dictionary, age: float) -> Dictionary:
		return project_payload(payload, _restore_projection, age)


	# How far the predicted pose sits from where the authoritative payload says it
	# should be, measured over the derivative-declaring fields, which are the ones
	# a pose is expressed in.
	#
	# An entity that declares no derivative channel reports INF rather than zero.
	# There is no pose to measure, so the recovery is not entitled to conclude it
	# is below the teleport threshold, and INF is what makes it restore the whole
	# closure instead of withholding fields on the strength of a measurement that
	# never happened.
	func _pose_error_against(payload: Dictionary) -> float:
		if _restore_projection.is_empty():
			return INF
		var span := clampi(_handle.ack_age_ticks, 0, _handle.max_restore_ticks)
		var target := _extrapolated_payload(payload, float(span) * _tick_delta)
		var current := _capture()
		var error := 0.0
		for field: StringName in _restore_projection:
			if current.has(field) and target.has(field):
				error = maxf(
					error,
					PredictionHandle._error(
						current[field],
						target[field],
						_angle_fields.has(field),
					),
				)
		return error


	# The fields a sub-teleport recovery leaves on the predicted body entirely,
	# read off the teleport_only() marks at wire time. A converging field is not
	# among them: it is written, just not all the way, so withholding it too
	# would be declaring the same field twice.
	func _withheld_below_teleport() -> Dictionary:
		return _withheld_fields


	# The converge rates in force for this recovery, one per property that declared
	# a rate with converge(rate).
	func _converge_rules_now() -> Dictionary:
		return _converge_rules


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


	# Checks the declared property classes against the tolerances and the wire
	# grammar that read them. Every trigger here is a declaration that cannot mean
	# what it says, so it is caught where the entity is wired rather than at the
	# first divergence it would silently misjudge.
	func _validate_property_classes(set: NetwSyncSet) -> void:
		# A predicted entity rewires on every role change, so the report is keyed
		# to the config it judges and fires only when that config changes, not once
		# per rewire. A masked causal property is NOT reported: the reconstruction
		# invariant means the owner rebuilds the full row from the masked stream and
		# can reconcile it, so riding a masked set is no longer a mistake.
		var config_hash := _property_class_report_hash(set)
		if config_hash == _validated_class_hash:
			return
		_validated_class_hash = config_hash
		var unquantized: Array[String] = []
		var uncompared: Array[String] = []
		for field: NetwSyncSet.Field in set.fields:
			match field.property_class:
				NetwSyncSet.PropertyClass.CAUSAL:
					if not field.quantizer:
						unquantized.append(String(field.key))
				NetwSyncSet.PropertyClass.COSMETIC:
					if field.epsilon_override >= 0.0:
						uncompared.append(String(field.key))
		if not unquantized.is_empty():
			Netw.dbg.warn(
				(
						"NetwLagCompensation: causal state properties [%s] have "
						+ "no quantizer, so their canonical form is their raw "
						+ "bits. Legal, but two peers agree only if they produce "
						+ "those bits identically."
				),
				[", ".join(unquantized)],
				func(m): push_warning(m),
			)
		if not uncompared.is_empty():
			push_error(
				(
						"PredictionComponent: cosmetic state properties [%s] "
						+ "carry a divergence epsilon. A cosmetic field is never "
						+ "compared, so the threshold decides nothing. Mark them "
						+ "derived() or drop the epsilon() mark."
				) % [", ".join(uncompared)],
			)
		_report_authority_model_mismatches()


	# The two provable authority-model contradictions an entity can wire with a
	# prediction engine attached. Each is a declaration that cannot mean what it
	# says, never a heuristic: a broadcast field is trusted to its owner, so
	# predicting the entity that owns it contradicts the field's own kind, and
	# an input set with no controller has no author, so nothing will ever drive
	# the prediction it exists to feed.
	func _report_authority_model_mismatches() -> void:
		var node := _entity.owner if _entity else null
		var script := node.get_script() as Script if is_instance_valid(node) \
				else null
		if script:
			var broadcast_set := NetwSyncSet.from_script(
				script,
				NetwSyncSet.Record.RECORD_BROADCAST,
			)
			if broadcast_set and not broadcast_set.fields.is_empty():
				var keys := PackedStringArray()
				for field: NetwSyncSet.Field in broadcast_set.fields:
					keys.append(String(field.key))
				push_warning(
					(
							"Prediction: %s declares broadcast() fields [%s] and "
							+ "carries a PredictionComponent. A broadcast field "
							+ "trusts its owner outright, so predicting the "
							+ "entity that owns it is a contradiction. Mark the "
							+ "fields state(), or drop the component."
					) % [_entity.entity_id, ", ".join(keys)],
				)
		if _input_binding and _input_binding.set \
				and not _input_binding.set.fields.is_empty() \
				and _entity.controller <= 0:
			push_warning(
				(
						"Prediction: %s declares input() fields but no peer "
						+ "controls it. Nothing will author the command stream, "
						+ "so the entity will never predict or consume. Assign "
						+ "a controller or drop the input marks."
				) % [_entity.entity_id],
			)


	# The config a property-class report judges, folded to one hash so the report
	# fires once per distinct config rather than once per rewire.
	func _property_class_report_hash(set: NetwSyncSet) -> int:
		var parts := PackedStringArray()
		for field: NetwSyncSet.Field in set.fields:
			parts.append("%s:%d:%d:%.4f" % [
				field.key,
				field.property_class,
				1 if field.quantizer else 0,
				field.epsilon_override,
			])
		parts.append("masked:%d" % (1 if set.masked else 0))
		parts.append("controller:%d" % (1 if _entity.controller > 0 else 0))
		return hash("
".join(parts))


	# Resolves the two axes onto the handle and returns the role they name. The
	# axes are the decision. The role is the name that decision has always had,
	# derived rather than chosen, so the two can never disagree.
	func _resolve_axes() -> PredictionHandle.Role:
		var is_server := _entity.is_authority
		var controlled_here := _entity.is_controlled_locally
		if controlled_here:
			_handle.input_source = PredictionHandle.InputSource.LOCAL
			# A closed-delay entity is simulated only where its commands are
			# authoritative. The peer that authors the command still authors it,
			# which is why the input axis is untouched, but it displays the answer
			# instead of guessing it. That is the whole mechanism: the speculative
			# mode is never taken, so no speculative transition exists to diverge.
			_handle.sim_mode = (
					PredictionHandle.SimMode.AUTHORITATIVE if is_server
					else PredictionHandle.SimMode.DISPLAY if _delay_closed()
					else PredictionHandle.SimMode.SPECULATIVE
			)
		elif is_server:
			_handle.input_source = PredictionHandle.InputSource.RECEIVED
			_handle.sim_mode = PredictionHandle.SimMode.AUTHORITATIVE
		else:
			_handle.input_source = PredictionHandle.InputSource.NONE
			_handle.sim_mode = PredictionHandle.SimMode.DISPLAY
		return PredictionHandle.role_for_axes(
			_handle.input_source,
			_handle.sim_mode,
		)


	# True when this entity declared that it never speculates. The policy is read
	# as declared rather than resolved, because the derivation behind
	# resolved_recovery_policy only ever answers with one of the two rebases:
	# closing the delay is a claim only the game can make about its own entity,
	# never one a body type can imply.
	func _delay_closed() -> bool:
		return _handle.recovery_policy \
				== PredictionHandle.RecoveryPolicy.DELAY_CLOSED


	# Opens the recovery cooldown from the freshest predict tick, and
	# with it the out-of-domain window when the contact was against a body this
	# peer does not simulate equivalently. Predict role only, so a server or
	# remote peer that never springs ignores it.
	func notify_contact() -> void:
		if _role != PredictionHandle.Role.PREDICT:
			return
		_cooldown_until_tick = _latest_input_tick + _handle.collision_cooldown_ticks
		_report_island_gap()
		if not _contact_is_equivalent():
			_out_of_domain_until = window_after(
				_latest_input_tick,
				_handle.collision_cooldown_ticks,
				_out_of_domain_until,
			)


	# True only when every body this entity could have touched is one this peer
	# simulates the way authority does.
	#
	# Equivalence is read off the axes rather than measured off geometry: a peer
	# whose simulation of a participant is DISPLAY is running a frozen proxy, and
	# contact against a frozen proxy is definitionally not the contact authority
	# resolved. That is a declaration mismatch, not a tolerance question, so no
	# amount of geometric agreement would change the answer.
	#
	# An undeclared island answers false rather than true. A contact against a
	# body nobody declared is exactly where a peer is least entitled to
	# exactness, so an absent declaration has to read as unknown and never as
	# equivalent. The warning names the gap; until someone closes it the label
	# refuses to claim an exactness it cannot support.
	func _contact_is_equivalent() -> bool:
		var participants := _island_participants()
		if participants.is_empty():
			return false
		for participant in participants:
			var handle := participant.prediction as PredictionHandle
			if handle == null \
					or handle.sim_mode == PredictionHandle.SimMode.DISPLAY:
				return false
		return true


	# The declared participants that are still live entities.
	func _island_participants() -> Array[NetwEntity]:
		var out: Array[NetwEntity] = []
		var declared: Array = _handle.island_config.get(&"participants", [])
		for entry in declared:
			var participant := entry as NetwEntity
			if participant and is_instance_valid(participant):
				out.append(participant)
		return out


	# Names the gap once when a contact is reported against no declared island at
	# all. Silence there would let an entity look in-domain forever while
	# colliding with bodies nobody ever claimed the peers agree about.
	func _report_island_gap() -> void:
		if _island_gap_reported or not _handle.island_config.get(
				&"participants", [],
		).is_empty():
			return
		_island_gap_reported = true
		push_warning(
			"Prediction: %s reported contact with no declared island. Declare the "
			% _entity.entity_id
			+ "bodies it touches through configure_island(), or its contact "
			+ "transitions will keep claiming an exactness they cannot hold.",
		)


	# True while a sub-teleport recovery is paused: the body is asleep or inside a
	# post-contact cooldown, where predicted and authoritative legitimately differ
	# and writing would fight the solver.
	func _corrections_suppressed() -> bool:
		return _handle.sleeping or _latest_input_tick < _cooldown_until_tick

	# --- Predict (owning client) ---


	# Captures a tick-labeled input without driving. When another tick arrives in
	# the same frame, the older label carries the pre-solve state.
	func _predict_author_tick(tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		if _latest_input_tick > _last_driven_input_tick:
			_timeline.record_state(_latest_input_tick + 1, _capture())
			_last_recorded_input_tick = _latest_input_tick
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_frame_input = input
		_input_binding.authored_tick = tick


	# Applies the newest authored input once. A zero-tick frame repeats the last
	# input but authors no new state label.
	func _predict_frame_step(timing: PredictTiming) -> void:
		var plan := predict_fold(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_author_tape_entry(label, fresh)
		_record_drive(_last_driven_entry_index, label, plan[&"kind"], _frame_input)
		_run(_frame_input, timing.delta, label, fresh)
		if fresh:
			_last_driven_input_tick = label
		_send_command_frame()


	func _predict_step(delta: float, tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		# A TICK drive is its own transition, so the tape entry is degenerate:
		# index, label, and tick are the same number and every entry is fresh.
		# The lane codec implies contiguous indices, so a clock re-anchor that
		# gaps the tick sequence starts a new tape epoch instead of straddling it.
		if not _authored_tape.is_empty() \
				and int(_authored_tape.back().get("index", -1)) != tick - 1:
			_tape_epoch = (_tape_epoch + 1) & 0xFF
			_authored_tape.clear()
			_journal.clear(_tape_epoch)
			_ack_of_acks = -1
		_next_tape_entry_index = tick
		_author_tape_entry(tick, true)
		_record_drive(tick, tick, PredictionHandle.DriveKind.FRESH, input)
		_run(input, delta, tick, true)
		var state := _capture()
		_timeline.record_state(tick + 1, state)
		_journal.close(tick, _state_fingerprint(state))
		# The row is closed before the send, so the frame's fingerprint section
		# can claim the transition it just drove rather than trailing by a tick.
		_send_command_frame()


	# Drives reconciliation from a received state frame. The set handle fires this
	# with the decoded header after the authoritative row arrives (the predicted body
	# is not snapped, since the predict binding is write-gated).
	func _on_state_frame(header: Dictionary) -> void:
		_stream_reconstructed = _stream_reconstructed \
				or bool(header.get("whole", true))
		_on_state(
			int(header.get("tick", -1)),
			int(header.get("ack", -1)),
			header.get("payload", { }),
		)


	func _on_state(recv_tick: int, ack: int, payload: Dictionary) -> void:
		if ack < 0:
			return
		var frame_entry: Dictionary = { }
		var ack_label := ack
		var predicted: Dictionary
		if _handle.schedule == PredictionHandle.Schedule.FRAME:
			frame_entry = _authored_entry_at(ack)
			if frame_entry.is_empty():
				return
			ack_label = int(frame_entry.get("label", -1))
			predicted = _entry_history.state_at(ack + 1)
			if predicted.is_empty():
				return
			_handle.ack_age_ticks = maxi(
				0,
				_next_tape_entry_index - ack - 1,
			)
			_handle.last_compare_staleness = 0
		else:
			_handle.ack_age_ticks = maxi(0, _latest_input_tick - ack)
			predicted = _timeline.latest_state_at_or_before(ack + 1)
			var compared_tick := \
					_timeline.latest_state_tick_at_or_before(ack + 1)
			_handle.last_compare_staleness = (
					-1 if compared_tick < 0 else ack + 1 - compared_tick
			)
		# A correction may consume any frame once the stream has reconstructed,
		# i.e. seen its gain-edge full row. Past that edge each masked frame merges
		# into the sender's coherent row for its tick, so the comparison and any
		# rebase land on a state that truly existed rather than on a partial
		# mosaic. Before the edge corrections wait. An unmasked set arms on its
		# first frame, so the gate is transparent to every non-masked recipe.
		var divergence := 0.0
		var corrected := false
		var domain := _journal.domain_at(ack)
		if _stream_reconstructed:
			# The row is read after the fingerprint pass, so a transition this
			# frame's own state frame was able to judge is judged by that verdict
			# rather than waiting a frame for its label to be worth reading. Both
			# reads are scalar: this runs on every authoritative frame of every
			# predicted entity, where materializing a row would allocate to deliver
			# two bytes.
			var verdict := evaluate(
				domain,
				_exact_verdict_of(_journal.flags_at(ack)),
				predicted,
				payload,
				_handle.divergence_epsilon,
				_epsilon_overrides,
				_trigger_excludes,
				_angle_fields,
				_handle.last_field_divergence,
			)
			divergence = verdict[&"divergence"]
			corrected = verdict[&"corrected"]
		var attribution := _attribution_for(ack)
		if corrected:
			_handle.divergence_detected.emit(ack, attribution)
			# Re-resolve from the handle so a runtime correction_mode change is live.
			_correction = PredictionHandle.resolve_correction_mode_for(
				_entity.owner,
				_handle.correction_mode,
			)
			_handle.is_reconciling = true
			_handle.corrections += 1
			var before := _capture()
			var correction_write: Dictionary = { }
			# A promoted recovery reports an unmeasurable pose error rather than
			# its measured one: past the escalation the recovery is no longer
			# entitled to claim it sits below the teleport tier, whatever the
			# measurement says.
			var escalated := _escalate_next
			_escalate_next = false
			# The recorded and compared payload stays the raw authoritative state.
			# Only the body write is what the recovery staged.
			var plan := recover(
				payload,
				_handle.resolved_recovery_policy(),
				_correction,
				_handle.snap_restore,
				guard_projection(
					_restore_projection,
					_handle.last_field_divergence,
					_handle.divergence_epsilon,
					_epsilon_overrides,
				),
				_withheld_below_teleport(),
				_converge_rules_now(),
				before,
				_angle_fields,
				INF if escalated else _pose_error_against(payload),
				_handle.teleport_threshold,
				_corrections_suppressed(),
				_handle.ack_age_ticks,
				_handle.max_restore_ticks,
				_tick_delta,
				domain,
				attribution,
				_out_of_domain_until >= 0 and ack_label < _out_of_domain_until,
			)
			_last_correction_teleported = plan[&"teleport"]
			_track_recovery_convergence(
				escalated, plan, divergence, predicted, payload,
			)
			if not plan[&"skip"]:
				_restore(plan[&"restore"])
				correction_write = plan[&"write"]
			# Anchor the authoritative state at its keyed tick so a later packet
			# carrying the same ack compares against the corrected value, not the
			# stale prediction it just replaced. Without this a duplicate ack (the
			# server is input starved and re-sends the same ack) re-triggers this
			# correction every tick until the ack advances past the stale entry.
			if _handle.schedule == PredictionHandle.Schedule.TICK:
				_timeline.record_state(ack + 1, payload)
			# REPLAY re-runs unacked inputs over the restored state (kinematic). SNAP
			# stops at the restore (dynamic): the predicted body resumes forward from
			# truth next tick and the display chase absorbs the snap, since a solver
			# body cannot be stepped per input without a physics fork.
			#
			# A recovery that declined to write has nothing to replay over. Re-running
			# the unacknowledged commands would advance the body from the prediction
			# the recovery just refused to correct, which is a repair the policy said
			# not to make.
			var replays: bool = not plan[&"skip"] \
					and _correction == PredictionHandle.CorrectionMode.REPLAY
			if replays and _handle.schedule == PredictionHandle.Schedule.FRAME:
				# A scope rollback re-runs the whole declared island over the same
				# entries. Every other policy re-runs this entity alone, which is
				# the same thing whenever the island is this entity.
				if _handle.resolved_recovery_policy() \
						== PredictionHandle.RecoveryPolicy.ROLLBACK_SCOPE:
					_replay_scope(ack)
				else:
					_replay_authored_entries(ack)
			elif replays:
				var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
				_handle.max_replay_depth = maxi(_handle.max_replay_depth, window.size())
				var live_input := _input_binding.snapshot_payload()
				for entry in window:
					_run(entry["input"], _tick_delta, entry["tick"], false)
					_timeline.record_state(entry["tick"] + 1, _capture())
				_input_binding.apply_payload(live_input)
			_emit_recovered(ack, attribution, before, correction_write)
			_handle.is_reconciling = false
		elif _stream_reconstructed:
			# An evaluation that found nothing to correct is the shrink the
			# escalation evidence was waiting for: the divergence closed, so the
			# next recovery starts a new sequence rather than inheriting an old
			# streak from one long past.
			_reset_recovery_trackers()

		if _handle.schedule == PredictionHandle.Schedule.FRAME:
			_timeline.trim_before(ack_label)
			_entry_history.trim_before(ack)
		else:
			_timeline.trim_before(ack)
		_handle.state_evaluated.emit(recv_tick, ack, divergence, corrected)


	# What a divergence at [param transition] is charged to.
	#
	# The acknowledgement lane is where both peers' command hashes and environment
	# digests meet, so a charge it already made is the best answer available and
	# is read off the transition's own journal row. Failing that, a substituted
	# row is still decisive on its own: authority declared it ran a command the
	# owner never authored, and no further evidence could change that.
	#
	# Anything else is UNATTRIBUTED rather than charged to the suspect that
	# happens to be tested last. A tolerance failure on an out-of-domain
	# transition is not a bug with an address, and naming one would turn the
	# attribution into a decoration.
	func _attribution_for(transition: int) -> NetwPredictJournal.Attribution:
		var charged := _journal.attribution_at(transition)
		if charged != NetwPredictJournal.Attribution.UNATTRIBUTED:
			return charged
		if _handle.last_attributed_transition == transition:
			return _handle.last_attribution
		if _journal.flags_at(transition) & NetwPredictJournal.ROW_SUBSTITUTED:
			return NetwPredictJournal.Attribution.COMMAND
		return NetwPredictJournal.Attribution.UNATTRIBUTED


	# Whether either fingerprint path has reached a verdict for a row yet.
	# Nothing infers a verdict from an unacked row: an in-domain transition
	# arriving before its acknowledgement falls back to the tolerance compare
	# rather than correcting against a comparison that never ran.
	func _exact_verdict_of(flags: int) -> ExactVerdict:
		if not (flags & NetwPredictJournal.ROW_ACKED):
			return ExactVerdict.UNJUDGED
		return ExactVerdict.EQUAL if flags & NetwPredictJournal.ROW_MATCHED \
				else ExactVerdict.UNEQUAL


	# Records whether the prediction for a transition fingerprinted equal to the
	# authority state that acknowledged it.
	#
	# Replays the client's unacknowledged FRAME entries after a kinematic restore.
	func _replay_authored_entries(ack: int) -> void:
		var live_input := _input_binding.snapshot_payload()
		var replay_input := _timeline.input_at(
			int(_authored_entry_at(ack).get("label", -1)),
		)
		var depth := 0
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index <= ack:
				continue
			var label := int(entry.get("label", -1))
			if bool(entry.get("fresh", false)):
				var authored_input := _timeline.input_at(label)
				if not authored_input.is_empty():
					replay_input = authored_input
			_run(replay_input, _tick_delta, label, false)
			_close_replayed_entry(index)
			depth += 1
		_input_binding.apply_payload(live_input)
		_handle.max_replay_depth = maxi(_handle.max_replay_depth, depth)


	# Records what one re-run entry produced, the write every replay path owes
	# whether it re-ran this entity alone or as one member of a scope.
	func _close_replayed_entry(index: int) -> void:
		var state := _capture()
		_entry_history.record_state(index + 1, state)
		_journal.close(index, _state_fingerprint(state))


	# The entries this member authored past [param ack], each carried with the
	# command that drove it. A scope rollback needs every member's entries in hand
	# before it steps any of them, because the members advance together through one
	# entry at a time rather than one member at a time.
	func _scope_entries(ack: int) -> Array[Dictionary]:
		var out: Array[Dictionary] = []
		var replay_input := _timeline.input_at(
			int(_authored_entry_at(ack).get("label", -1)),
		)
		for entry: Dictionary in _authored_tape:
			var index := int(entry.get("index", -1))
			if index <= ack:
				continue
			var label := int(entry.get("label", -1))
			if bool(entry.get("fresh", false)):
				var authored_input := _timeline.input_at(label)
				if not authored_input.is_empty():
					replay_input = authored_input
			out.append({
				&"index": index,
				&"label": label,
				&"input": replay_input,
			})
		return out


	# True when this peer can re-run this member's own step. A member this peer
	# only displays is a proxy playing back a stream, so there is no step to
	# re-run and guessing one would invent a history authority never had.
	func _is_steppable() -> bool:
		return _handle.sim_mode != PredictionHandle.SimMode.DISPLAY \
				and _handle.simulate.is_valid() \
				and _handle.schedule == PredictionHandle.Schedule.FRAME


	# The members a scope rollback re-runs, in entity-id order.
	#
	# The order is the whole reason this is not a set. A cross-entity write lands
	# in the order the members ran, so a resim that visited them in a different
	# order would reach a different state from the same commands.
	func _scope_members(ack: int) -> Array:
		var out: Array = [self]
		var iface := _iface()
		if iface:
			for participant: NetwEntity in _island_participants():
				var engine := iface._engines.get(participant) as _PredictionEngine
				if not engine or engine == self or not engine._is_steppable():
					continue
				# A member with no recorded state at the rollback point was not
				# simulating then, so there is nothing to roll it back to.
				if engine.transition_state_at(ack).is_empty():
					continue
				out.append(engine)
		out.sort_custom(
			func(a: _PredictionEngine, b: _PredictionEngine) -> bool:
				return a.order_key() < b.order_key(),
		)
		return out


	# Restores every declared island member to the rollback point and re-runs them
	# together, one entry at a time, in entity-id order within each entry.
	#
	# Every member is re-run unconditionally. Gating a member on whether it looked
	# like it diverged is what erases a cross-entity write: the push one member
	# applied to another only exists because the pusher ran, so a rollback that
	# re-runs the pusher and holds the pushed keeps the cause and drops the
	# effect. Re-running everyone regenerates the interaction instead of
	# preserving a stale copy of its result.
	func _replay_scope(ack: int) -> void:
		var members := _scope_members(ack)
		if members.size() <= 1:
			_replay_authored_entries(ack)
			return
		var plans: Array[Dictionary] = []
		var last_entry := ack
		for member: _PredictionEngine in members:
			var entries := member._scope_entries(ack)
			for entry: Dictionary in entries:
				last_entry = maxi(last_entry, int(entry[&"index"]))
			plans.append({
				&"member": member,
				&"entries": entries,
				&"live": member._input_binding.snapshot_payload(),
			})
			# This entity was already put back by the recovery that staged it, so
			# restoring it again would overwrite authority's payload with the
			# prediction the recovery just replaced.
			if member != self:
				member._restore(member.transition_state_at(ack))
		for index in range(ack + 1, last_entry + 1):
			for plan: Dictionary in plans:
				var member: _PredictionEngine = plan[&"member"]
				for entry: Dictionary in plan[&"entries"]:
					if int(entry[&"index"]) != index:
						continue
					member._run(
						entry[&"input"],
						member._tick_delta,
						int(entry[&"label"]),
						false,
					)
					member._close_replayed_entry(index)
					break
		for plan: Dictionary in plans:
			var member: _PredictionEngine = plan[&"member"]
			member._input_binding.apply_payload(plan[&"live"])
			member._handle.max_replay_depth = maxi(
				member._handle.max_replay_depth,
				(plan[&"entries"] as Array).size(),
			)


	# Diffs against the pre-correction capture. Staged writes override immediate
	# readback because a PhysicsServer-backed setter may not sync its Node until
	# the next physics frame.
	func _emit_recovered(
			transition: int,
			attribution: NetwPredictJournal.Attribution,
			before: Dictionary,
			correction_write: Dictionary = { },
	) -> void:
		var after := _capture()
		for field: StringName in correction_write:
			after[field] = correction_write[field]
		var deltas: Dictionary = { }
		for field: StringName in after:
			if not before.has(field):
				continue
			var delta := _pose_delta(
				after[field],
				before[field],
				_angle_fields.has(field),
			)
			if delta == null or _delta_negligible(delta):
				continue
			deltas[field] = delta
		if deltas.is_empty():
			return
		_handle.recovered.emit(
			transition,
			deltas,
			_last_correction_teleported,
			attribution,
		)


	func _delta_negligible(delta: Variant) -> bool:
		match typeof(delta):
			TYPE_FLOAT:
				return absf(delta as float) < 0.000001
			TYPE_VECTOR2:
				return (delta as Vector2).length() < 0.000001
			TYPE_VECTOR3:
				return (delta as Vector3).length() < 0.000001
		return true


	# Feeds one staged recovery into the escalation evidence, or spends it. A
	# skipped recovery repaired nothing so it neither counts nor resets, a
	# replay converges by construction, and a teleport is already the full
	# closure escalation would promote to, so it starts the count over.
	func _track_recovery_convergence(
			escalated: bool,
			plan: Dictionary,
			divergence: float,
			predicted: Dictionary,
			payload: Dictionary,
	) -> void:
		if escalated:
			_reset_recovery_trackers()
			# The promoted closure lands mid-disturbance, so the same settling
			# window a fresh contact earns covers the snap it just applied.
			_cooldown_until_tick = _latest_input_tick \
					+ _handle.collision_cooldown_ticks
			return
		if plan[&"skip"] \
				or _correction == PredictionHandle.CorrectionMode.REPLAY:
			return
		if plan[&"teleport"]:
			_reset_recovery_trackers()
			return
		var verdict := escalation_after(
			_nonshrink_streak,
			_last_recovery_sign,
			_last_recovery_divergence,
			divergence,
			_divergence_sign(predicted, payload),
		)
		_nonshrink_streak = verdict[&"streak"]
		_last_recovery_sign = verdict[&"sign"]
		_last_recovery_divergence = divergence
		_escalate_next = verdict[&"escalate"]


	# Forgets the recovery-convergence evidence. Called where the divergence
	# story restarts: a shrink to agreement, a teleport, a rewire, an epoch
	# bump, and the escalation the evidence just spent itself on.
	func _reset_recovery_trackers() -> void:
		_nonshrink_streak = 0
		_last_recovery_sign = 0
		_last_recovery_divergence = -1.0
		_escalate_next = false


	# The dominant-axis sign of the divergence this recovery answers, read off
	# the field the last comparison charged the most error to.
	func _divergence_sign(predicted: Dictionary, payload: Dictionary) -> int:
		var dominant := StringName()
		var top := 0.0
		for field: StringName in _handle.last_field_divergence:
			var error := float(_handle.last_field_divergence[field])
			if error > top and predicted.has(field) and payload.has(field):
				top = error
				dominant = field
		if dominant == StringName():
			return 0
		return delta_sign(_pose_delta(
			payload[dominant],
			predicted[dominant],
			_angle_fields.has(dominant),
		))

	# --- Host-local (listen-server host controlling its own entity) ---


	func _host_local_author_tick(tick: int) -> void:
		var input := _input_binding.snapshot_payload()
		if _timeline:
			_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_frame_input = input
		_input_binding.authored_tick = tick


	func _host_local_frame_step(timing: PredictTiming) -> void:
		var plan := predict_fold(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_record_drive(label, label, plan[&"kind"], _frame_input)
		_run(_frame_input, timing.delta, label, fresh)
		_ack_advanced = fresh
		if fresh:
			_last_driven_input_tick = label
			_state_binding.authored_tick = label
			_state_binding.reconcile_ack = label


	func _host_local_step(delta: float, tick: int) -> void:
		# The host is the authority and the controller at once, so it simulates from
		# its own gathered input and publishes the result. No prediction, no
		# reconciliation against itself.
		var input := _input_binding.snapshot_payload()
		if _timeline:
			_timeline.record_input(tick, input)
		_record_drive(tick, tick, PredictionHandle.DriveKind.FRESH, input)
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
				samples = [{ "tick": tick, "payload": header.get("payload", { }) }]
		var oldest := -1
		for sample: Dictionary in samples:
			var stick := int(sample.get("tick", -1))
			if stick < 0:
				continue
			_timeline.record_input(stick, sample.get("payload", { }))
			oldest = stick if oldest < 0 else mini(oldest, stick)
		if _next_input_tick < 0 and oldest >= 0:
			_next_input_tick = oldest


	# Consumes at most one queued transition per tick, through the same standing
	# buffer verdict the FRAME tier replays under, with the depth measured in
	# ticks rather than in entries.
	#
	# One tick advances the ack by one. A backlog is worked off at the tick rate
	# rather than by consuming several at once, so authority never runs a
	# transition its own clock has not reached, and a peer reading the ack cannot
	# see it jump a span no single tick produced.
	func _consume_step(delta: float, server_tick: int) -> void:
		var previous_ack := _ack
		if _next_input_tick >= 0:
			_resync_if_stranded()
			var plan := consume_plan(
				_queued_span(),
				maxi(0, _handle.consume_buffer_ticks),
				_consume_warmed,
			)
			_consume_warmed = plan[&"warmed"]
			match plan[&"action"]:
				ConsumeAction.REPLAY:
					_consume_one(delta)
				ConsumeAction.HOLD:
					_handle.held_count += 1
					_mark_idle_drive(_ack, PredictionHandle.DriveKind.HOLD)
				ConsumeAction.STARVED:
					_handle.starved_count += 1
					_mark_idle_drive(_ack, PredictionHandle.DriveKind.STARVED)
		_ack_advanced = _ack != previous_ack
		# A held authority tick still owes every remote observer its state, so
		# the frame flows either way. It acknowledges nothing when the consume
		# did not advance: the body has coasted past the ack, and re-stamping it
		# would frame the coast as divergence the owner did not cause.
		_state_binding.authored_tick = server_tick
		_state_binding.reconcile_ack = _ack if _ack_advanced else -1
		_handle.ack_age_ticks = maxi(0, _timeline.newest_input_tick() - _ack)
		# Authoritative state is recorded by the recorder after the tick, so the
		# acknowledgement run sent here covers through the previous tick's close.
		_send_ack_frame()


	# Replays one client-authored tape entry while maintaining the FRAME buffer.
	func _consume_frame_step(timing: PredictTiming) -> void:
		var previous_ack := _ack
		_last_replayed_fresh = false
		var depth := _replay_depth()
		var buffer := maxi(0, _handle.replay_buffer_depth)
		var plan := consume_plan(depth, buffer, _replay_warmed)
		_replay_warmed = plan[&"warmed"]
		match plan[&"action"]:
			ConsumeAction.REPLAY:
				_resync_tape_if_stranded(depth, buffer)
				_replay_tape_entry(timing.delta)
			ConsumeAction.HOLD:
				_handle.held_count += 1
				_mark_idle_drive(
					_last_replayed_label,
					PredictionHandle.DriveKind.HOLD,
				)
			ConsumeAction.STARVED:
				_handle.starved_count += 1
				_mark_idle_drive(
					_last_replayed_label,
					PredictionHandle.DriveKind.STARVED,
				)
		_ack_advanced = _ack != previous_ack
		# The frame flows on a held pass too, acknowledging nothing, so a remote
		# observer's stream never gaps while the owner's ack stalls.
		_state_binding.authored_tick = timing.tick
		_state_binding.reconcile_ack = _ack if _ack_advanced else -1
		_refresh_tape_diagnostics()
		_handle.ack_age_ticks = _replay_depth()
		_send_ack_frame()


	# Applies the tape entry at the replay cursor and acknowledges its index.
	func _replay_tape_entry(delta: float) -> void:
		var entry := _replay_entry(_replay_cursor)
		if entry.is_empty():
			return
		var label := int(entry.get("label", -1))
		var fresh := bool(entry.get("fresh", false))
		var input := _last_input
		var kind := PredictionHandle.DriveKind.REPEAT
		var applied_fresh := false
		if fresh:
			var command := _command_for(entry, label)
			if not command.is_empty():
				input = command
				_last_input = input
				kind = PredictionHandle.DriveKind.FRESH
				applied_fresh = true
			else:
				_handle.missing_count += 1
				kind = PredictionHandle.DriveKind.MISSING
		if input.is_empty():
			input = _stall_input
		_record_drive(_replay_cursor, label, kind, input)
		_run(input, delta, label, applied_fresh)
		_ack = _replay_cursor
		_replay_cursor += 1
		_last_replayed_label = label
		_last_replayed_fresh = fresh
		_handle.consumed_count += 1


	# Re-opens a stranded FRAME cursor behind the configured standing buffer.
	func _resync_tape_if_stranded(depth: int, buffer: int) -> void:
		var ceiling := _handle.max_consume_lag_ticks
		if ceiling <= 0 or depth <= buffer + ceiling:
			return
		var target := _replay_cursor + depth - buffer - 1
		if target <= _replay_cursor:
			return
		_declare_skipped(_replay_cursor, target)
		_handle.skipped_count += target - _replay_cursor
		_handle.resync_count += 1
		_replay_cursor = target


	# Records the transitions a resync stepped over as ones authority never ran.
	#
	# A skipped transition is the one substitution the honest protocol still
	# admits, so it is journaled and acknowledged rather than left as a silent
	# gap. An owner that sees a gap cannot tell a skip from a lost frame, while
	# a declared substitution names exactly which of its commands never ran.
	func _declare_skipped(from: int, until: int) -> void:
		for transition in range(from, until):
			var queued: Dictionary = _replay_entry(transition)
			_journal.open(
				transition,
				int(queued.get("label", -1)),
				PredictionHandle.DriveKind.MISSING,
				0,
			)
			_journal.mark_substituted(transition)


	# Counts contiguous queued transitions beginning exactly at the replay cursor.
	func _replay_depth() -> int:
		if _replay_cursor < 0:
			return 0
		var depth := 0
		while _command_queue.has(_replay_cursor + depth):
			depth += 1
		return depth


	func _replay_entry(transition: int) -> Dictionary:
		return _command_queue[transition] \
				if _command_queue.has(transition) else { }


	# The command a fresh transition drove with. The owner lane carries it on the
	# transition itself, so there is nothing to look up and nothing to miss.
	func _command_for(entry: Dictionary, label: int) -> Dictionary:
		if entry.has("command"):
			return entry["command"]
		return _timeline.input_at(label) if _timeline.has_input_at(label) else { }


	# Selects at most max_consume_per_tick eligible labels. All older selected
	# labels fold into the newest one, which is the frame's only drive.
	func _fold_and_consume_frame(delta: float) -> void:
		var selected := _next_input_tick
		var allowance := maxi(1, _handle.max_consume_per_tick)
		var fold_budget := maxi(
			0,
			_queued_span() - _handle.consume_buffer_ticks - 1,
		)
		while allowance > 1 and fold_budget > 0 \
				and _input_is_eligible(selected + 1):
			selected += 1
			allowance -= 1
			fold_budget -= 1

		var folded := selected - _next_input_tick
		for tick in range(_next_input_tick, selected):
			if _timeline.has_input_at(tick):
				_handle.consumed_count += 1
			else:
				_handle.missing_count += 1
		_handle.folded_count += folded

		if _timeline.has_input_at(selected):
			var input := _timeline.input_at(selected)
			var kind := PredictionHandle.DriveKind.FRESH
			if folded > 0:
				kind = PredictionHandle.DriveKind.FOLD_DRIVE
			_record_drive(selected, selected, kind, input)
			_run(input, delta, selected, true)
			_last_input = input
			_handle.consumed_count += 1
		else:
			_handle.missing_count += 1
			_record_drive(
				selected,
				selected,
				PredictionHandle.DriveKind.MISSING,
				_missing_frame_input(),
			)
			_drive_missing_frame(delta, selected)
		_ack = selected
		_next_input_tick = selected + 1


	# A tick is eligible once it arrived or a later tick proves its absence.
	func _input_is_eligible(tick: int) -> bool:
		return _timeline.has_input_at(tick) \
				or _timeline.newest_input_tick() > tick


	func _drive_missing_frame(delta: float, label: int) -> void:
		_run(_missing_frame_input(), delta, label, false)


	# The input a missing label actually drives with, resolved once so the journal
	# fingerprints the command that ran rather than the one that was lost.
	func _missing_frame_input() -> Dictionary:
		if _handle.missing_policy == PredictionHandle.MissingInput.REPEAT_LAST \
				and not _last_input.is_empty():
			return _last_input
		return _stall_input


	# Jumps the cursor back to the live edge when it has fallen so far behind that
	# it cannot walk there. The drain never steps over an absent tick, so through a
	# hole the cursor gains one tick per server tick while the controller authors
	# one, and a gap opened by a clock re-anchor or a long stall never closes on
	# its own. Consuming second-old input is worse than skipping it, so past the
	# ceiling the cursor re-opens at the newest arrival behind the standing buffer.
	func _resync_if_stranded() -> void:
		var ceiling := _handle.max_consume_lag_ticks
		if ceiling <= 0 or _queued_span() <= ceiling:
			return
		var target := _timeline.newest_input_tick() - _handle.consume_buffer_ticks
		if target <= _next_input_tick:
			return
		# Skipped ticks are declared substituted like a FRAME resync's entries,
		# floored to the acknowledgement window since older transitions can never
		# ride an ack run anyway.
		for transition in range(
				maxi(_next_input_tick, target - ACK_WINDOW_MAX),
				target,
		):
			_journal.open(
				transition,
				transition,
				PredictionHandle.DriveKind.MISSING,
				0,
			)
			_journal.mark_substituted(transition)
		_handle.skipped_count += maxi(0, target - _next_input_tick)
		_handle.resync_count += 1
		_next_input_tick = target


	# Queued input ticks from the consume cursor through the newest arrival,
	# counting an unhealed hole as present since the cursor steps over it.
	func _queued_span() -> int:
		return _timeline.newest_input_tick() - _next_input_tick + 1


	func _consume_one(delta: float) -> void:
		if _timeline.has_input_at(_next_input_tick):
			var input := _timeline.input_at(_next_input_tick)
			_record_drive(
				_next_input_tick,
				_next_input_tick,
				PredictionHandle.DriveKind.FRESH,
				input,
			)
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
			# The row is journaled under both policies, so the acknowledgement run
			# stays contiguous and fingerprints the command that actually ran.
			_record_drive(
				_next_input_tick,
				_next_input_tick,
				PredictionHandle.DriveKind.MISSING,
				_missing_frame_input(),
			)
			if _handle.missing_policy == PredictionHandle.MissingInput.REPEAT_LAST \
					and not _last_input.is_empty():
				_run(_last_input, delta, _next_input_tick, true)
			# STALL applies no movement at all.
			_ack = _next_input_tick
			_next_input_tick += 1

	# --- Shared ---


	# The one choke point every real drive passes through, so the journal records
	# exactly the transitions that ran. An idle frame goes through
	# _mark_idle_drive instead and appends no row.
	func _record_drive(
			transition: int,
			label: int,
			kind: PredictionHandle.DriveKind,
			input: Dictionary,
	) -> void:
		_handle.drive_seq += 1
		_handle.last_drive_label = label
		_handle.last_drive_kind = kind
		_journal.open(
			transition,
			label,
			kind,
			NetwPredictJournal.fnv1a(_input_binding.canonical_bytes(input)),
		)
		# The environment is fingerprinted before the drive runs, because what
		# attribution needs to know is the world the transition ran AGAINST. A
		# digest taken afterward would describe the world the transition helped
		# make, which cannot exonerate or convict it.
		_e_digest = _sample_environment()
		_journal.mark_e_digest(transition, _e_digest)
		_journal.mark_domain(transition, domain_of(
			bool(_handle.island_config.get(&"declared", false)),
			bool(_handle.island_config.get(&"approximate", false)),
			label,
			_out_of_domain_until,
		))


	# Fingerprints the declared world facts this drive runs against. An entity
	# that declared no epoch and no sensors digests to a constant, which is the
	# honest answer: it declared nothing, so it can discover nothing.
	func _sample_environment() -> int:
		var epoch := int(_handle.island_config.get(&"epoch", -1))
		var sensors: Dictionary = _handle.island_config.get(&"sensors", { })
		# An entity that declared no world facts digests to the same zero an
		# unwritten row already holds, so the fold is skipped rather than run to
		# reach a foregone answer. Every drive of every predicted entity passes
		# through here, which makes a constant worth not recomputing.
		if epoch == -1 and sensors.is_empty():
			return 0
		if epoch != _island_epoch:
			# A world the game says changed version reopens the window, since the
			# peers cannot both have adopted the change on the same transition.
			if _island_epoch != -1:
				_out_of_domain_until = window_after(
					_latest_input_tick,
					_handle.collision_cooldown_ticks,
					_out_of_domain_until,
				)
			_island_epoch = epoch
			# The recoveries before the world changed were answering divergences
			# of a world that no longer exists, so their streak proves nothing
			# about the one that replaced it.
			_reset_recovery_trackers()
		var samples: Dictionary = { }
		for name: StringName in sensors:
			var sampler := sensors[name] as Callable
			if sampler.is_valid():
				samples[name] = sampler.call()
		_sensor_samples = samples
		return environment_digest(epoch, samples)


	func _mark_idle_drive(label: int, kind: PredictionHandle.DriveKind) -> void:
		_handle.last_drive_label = label
		_handle.last_drive_kind = kind


	# Authors exactly one contiguous tape entry for the FRAME drive about to run.
	func _author_tape_entry(label: int, fresh: bool) -> void:
		var entry := {
			"index": _next_tape_entry_index,
			"label": label,
			"fresh": fresh,
		}
		_authored_tape.append(entry)
		while _authored_tape.size() > TAPE_HISTORY_LIMIT:
			_authored_tape.remove_at(0)
		_last_driven_entry_index = _next_tape_entry_index
		_handle.tape_epoch = _tape_epoch
		_handle.last_transition_index = _next_tape_entry_index
		_next_tape_entry_index += 1


	func _authored_entry_at(index: int) -> Dictionary:
		for entry: Dictionary in _authored_tape:
			if int(entry.get("index", -1)) == index:
				return entry
		return { }


	func _refresh_tape_diagnostics() -> void:
		var indices: Array = _command_queue.keys()
		indices.sort()
		_handle.tape_epoch = _command_epoch
		_handle.tape_queue_depth = _replay_depth()
		_handle.last_transition_index = (
				int(indices.back()) if not indices.is_empty() else -1
		)


	func _run(input: Dictionary, delta: float, tick: int, is_fresh: bool) -> void:
		_input_binding.apply_payload(input)
		if _handle.simulate.is_valid():
			_handle.simulate.call(delta, tick, is_fresh)


	# Round-trips prediction input through its wire quantizers so both peers feed
	# the simulation the same canonical values.
	func _canonical_input(payload: Dictionary) -> Dictionary:
		return _input_binding.canonicalize_payload(payload)


	# Every recorded slot holds the canonical form, so a compare between a
	# prediction and the authority payload that produced it is an equality
	# question rather than a tolerance question.
	func _capture() -> Dictionary:
		return _state_binding.canonicalize_payload(
			_state_binding.snapshot_payload(),
		)


	func _state_fingerprint(payload: Dictionary) -> int:
		return NetwPredictJournal.fnv1a(
			_state_binding.canonical_bytes(payload),
		)


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
