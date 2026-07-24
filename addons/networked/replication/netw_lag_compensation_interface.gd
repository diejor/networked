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
## S' = F_H(S, C, E) -> (S', O)
##   S  state        the declared state fields, compared against authority
##   C  commands     the declared input fields, one set per tick
##   E  environment  the declared sensor, epoch, and island facts
##   H  topology     the schedule, participants, epoch, and reported body mode
##   F  transition   the game's simulate callable, run under H
##   O  witness      the realized contact classes and discrete solve facts
## [/codeblock]
## [code]S[/code] is what [method NetwScriptModel.PropertyConfig.state]
## declares, [code]C[/code] what [method NetwScriptModel.PropertyConfig.input]
## declares, [code]E[/code] what
## [method NetwLagCompensationInterface.PredictionHandle.sensors] and
## [method NetwLagCompensationInterface.PredictionHandle.island]
## declare, and [code]F[/code] is
## [member NetwLagCompensationInterface.PredictionHandle.simulate] run on the
## [enum NetwLagCompensationInterface.PredictionHandle.Schedule] cadence.
## [code]H[/code] is captured by the engine, and [code]O[/code] is declared by
## [method NetwLagCompensationInterface.PredictionHandle.witness].
## When a transition disagrees with authority,
## [signal NetwLagCompensationInterface.PredictionHandle.divergence_detected]
## charges the disagreement to one antecedent as a
## [enum NetwPredictJournal.Attribution], and a
## [enum NetwLagCompensationInterface.PredictionHandle.RecoveryPolicy] re-bases
## [code]S[/code].
##
## [br][br][b]The recovery guarantee[/b]
## [br]One disturbance episode is the unit of recovery. Inside it, an operator
## must shrink the aligned error or spend finite evidence toward one
## full-closure restore. A restore projects only while the channel error stays
## inside the destination's projected error budget, and a divergence outside
## the declared domain restores the whole closure. Exhausting the episode's
## finite evidence enters bounded
## [constant PredictionHandle.RecoveryPolicy.DELAY_CLOSED]
## quarantine and a journaled reseed. No repairing policy can produce a
## persistent oscillation. The telemetry-only
## [constant PredictionHandle.RecoveryPolicy.OBSERVE] policy writes nothing,
## so a persistent divergence may keep its report open without oscillating.
##
## [br][br][b]The transparency bargain[/b]
## [br]The engine explains only what was declared. An undeclared environment
## read leaves its transitions
## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN] and its divergences
## [constant NetwPredictJournal.Attribution.UNKNOWN]. Declaring the
## environment through
## [method NetwLagCompensationInterface.PredictionHandle.sensors],
## [method NetwLagCompensationInterface.PredictionHandle.epoch], and
## [method NetwLagCompensationInterface.PredictionHandle.island] is
## how attribution sharpens from "something differed" to the antecedent that
## differed. An undeclared
## [method NetwLagCompensationInterface.PredictionHandle.witness]
## leaves realized solve evidence unknown, so the ladder never guesses contact
## agreement from a missing observation. A declared witness reports only
## post-solve [code]O[/code]. It never becomes an antecedent. A malformed
## declaration is reported, while an absent declaration is valid and silent.
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
## the owner. The two do not mix on one field. A broadcast field under a
## [PredictionComponent], or a script declaring both state and input without a
## [PredictionComponent] or code-first registration, is a configuration error
## the validator reports.
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
##         if attribution == NetwPredictJournal.Attribution.CLOSURE:
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

const _PredictionBoundaryOverlay := preload(
	"res://addons/networked/debug/prediction_boundary_overlay.gd"
)
var _prediction_overlays: Dictionary[NetwEntity, Node] = { }

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
	_attach_prediction_overlay(entity)


## Releases [param entity]'s prediction engine record, restoring the set-handle
## hooks it held. [member NetwEntity.prediction] stays bound and keeps its config
## and counters, so a re-registration resumes where the engine left off.
func unregister_prediction(entity: NetwEntity) -> void:
	var engine := _engines.get(entity) as _PredictionEngine
	if not engine:
		return
	_engines.erase(entity)
	_detach_prediction_overlay(entity)
	engine._release()
	entity.prediction._engine_ref = null


# Adds the read-only in-world overlay when the shared debug gate is active.
func _attach_prediction_overlay(entity: NetwEntity) -> void:
	if not DebugFeature.is_world_debug_enabled():
		return
	if not bool(ProjectSettings.get_setting(
			"debug/networked/prediction_boundary_overlay",
			true,
	)):
		return
	if not is_instance_valid(entity.owner):
		return
	var overlay := _PredictionBoundaryOverlay.new()
	overlay.name = "PredictionBoundaryOverlay"
	overlay.bind(entity)
	_prediction_overlays[entity] = overlay
	entity.owner.add_child.call_deferred(overlay)


# Releases the entity overlay without changing any prediction state.
func _detach_prediction_overlay(entity: NetwEntity) -> void:
	var overlay := _prediction_overlays.get(entity) as Node
	_prediction_overlays.erase(entity)
	if is_instance_valid(overlay):
		overlay.queue_free()


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


# Drains every registered entity's public evidence to JSONL when the
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
			engine.prepare_island(PredictionHandle.Schedule.TICK)
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.network_tick(timing)


	func frame_step(timing: PredictTiming) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.prepare_island(PredictionHandle.Schedule.FRAME)
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


## Runtime prediction-boundary policy and evidence for one [NetwEntity].
##
## Every entity owns one stable handle through [member NetwEntity.prediction]. A
## [PredictionComponent] may supply scene policy, while code uses
## [method archetype], [method schedule], [method recovery], [method sensors],
## [method witness], [method transport], [method island], and [method epoch].
## Each fact must have one source.
##
## [br][br]The client compares its claim with the authority row named by the
## server acknowledgement. [member input_source] and [member sim_mode] state
## whether this peer authors, receives, predicts, or displays the entity.
## [method stats], [method journal], [method episode], and [method metrics]
## expose the resulting evidence without changing simulation.
## [codeblock]
## var pred := NetwEntity.of(self).prediction
## pred.archetype(
##     NetwLagCompensationInterface.PredictionHandle.Archetype.SOLVER_BODY,
## )
## pred.witness().contacts(sample_contacts)
## pred.recovery().on_breach(
##     NetwLagCompensationInterface.PredictionHandle.BreachResponse.DEMOTE,
## )
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
		## A replicated remote stepped locally with a predicted command.
		SIMULATE,
	}

	## Where this peer's copy of the entity gets the command it simulates with.
	##
	## [constant LOCAL] authors the command here. [constant RECEIVED] reads a
	## command another peer authored and sent. [constant PREDICTED] guesses a
	## command nobody sent for a simulated island participant.
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

	## Local collider realizations retained for diagnostics, never compared.
	enum ContactClass {
		## The solve reported no collider.
		NONE,
		## The collider is the support reported by the declared ground sensor.
		DECLARED_SUPPORT,
		## Static geometry other than the declared support.
		OTHER_STATIC,
		## A dynamic entity this peer predicts or simulates authoritatively.
		PREDICTED_DYNAMIC,
		## A dynamic entity outside this peer's prediction island.
		UNPREDICTED_DYNAMIC,
		## A kinematic or animated collision proxy.
		KINEMATIC_PROXY,
	}

	## Peer-invariant realized-witness classes carried by fingerprints and acks.
	enum WitnessClass {
		## No contact was realized.
		NONE = 0,
		## The declared support was contacted.
		SUPPORT = 1 << 0,
		## Static geometry other than the declared support was contacted.
		STATIC = 1 << 1,
		## A replicated dynamic entity was contacted.
		DYNAMIC_ENTITY = 1 << 2,
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
	## An archetype is a floor, not a cage: [method archetype] applies
	## its bundle, and any value declared in the scene or configured afterward
	## overrides the bundled one.
	enum Archetype {
		## No preset. Every knob keeps its own default until declared.
		NONE,
		## A body whose step is a plain callable, re-runnable within one frame.
		## Bundles the [constant Schedule.TICK] cadence, the
		## [constant MissingInput.STALL] hold,
		## [constant RecoveryPolicy.REBASE_REPLAY], and
		## [constant BreachResponse.PREDICT_THROUGH], because a command-pinned
		## velocity re-synchronizes on the next tick and a breach cannot
		## compound.
		KINEMATIC,
		## A solver-integrated body whose step cannot be re-run per input.
		## Bundles the [constant Schedule.FRAME] cadence, the
		## [constant MissingInput.REPEAT_LAST] hold,
		## [constant RecoveryPolicy.REBASE_RECOVER], the
		## [constant RestoreMode.EXTRAPOLATED] projection, a teleport
		## threshold sized for a body that settles through contacts, and
		## [constant BreachResponse.DEMOTE], armed only once
		## [method witness] declares the contact observation, because a
		## breach impulse into integrating momentum compounds every tick.
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
	## whose step cannot be re-run cheaply.
	## [constant DELAY_CLOSED] never speculates, so no divergence can arise.
	## [constant OBSERVE] reports a divergence and repairs nothing. It is outside
	## the finite recovery guarantee: persistent divergence can retain one open
	## episode, but it cannot create a recovery oscillation because it writes no
	## operator.
	enum RecoveryPolicy {
		REBASE_REPLAY,
		REBASE_RECOVER,
		DELAY_CLOSED,
		OBSERVE,
	}

	## How a prediction-island member is represented on this peer.
	enum Fidelity {
		## Display received state and classify contact against a proxy.
		PROXY,
		## Step the member locally with substituted commands.
		SIMULATED,
	}

	## How divergence is reconciled across an island.
	enum Reconcile {
		## Reconcile each predicted entity independently.
		INDEPENDENT,
		## Restore and replay the island together. Reserved until useful.
		JOINT,
	}

	## What prediction does when its realized witness touches a body outside the
	## prediction boundary.
	##
	## Static world geometry is part of the shared world every peer solves
	## against, so touching it never breaches. A breach is contact with a
	## dynamic body this peer does not step, whose displayed pose is a stale
	## stand-in the solve cannot reproduce.
	enum BreachResponse {
		## Continue speculation and let ordinary recovery absorb any divergence.
		PREDICT_THROUGH,
		## Follow authority immediately while commands continue to flow.
		DEMOTE,
	}

	## Fluent declaration of prediction cadence and missing-input behavior.
	class ScheduleConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Selects one drive per network tick and returns this configurator.
		func tick() -> ScheduleConfig:
			if _handle._allows_code_value(&"tier", "schedule().tick"):
				_handle._schedule = Schedule.TICK
			return self


		## Selects one drive per physics frame and returns this configurator.
		func frame() -> ScheduleConfig:
			if _handle._allows_code_value(&"tier", "schedule().frame"):
				_handle._schedule = Schedule.FRAME
			return self


		## Keeps [param depth] FRAME transitions buffered.
		func buffer_depth(depth: int) -> ScheduleConfig:
			if _handle._allows_code_value(
					&"buffer_depth",
					"schedule().buffer_depth",
			):
				_handle.replay_buffer_depth = maxi(0, depth)
			return self


		## Stalls when the next expected input is absent.
		func hold_stall() -> ScheduleConfig:
			if _handle._allows_code_value(&"hold", "schedule().hold_stall"):
				_handle.missing_policy = MissingInput.STALL
			return self


		## Repeats the last input when the next expected one is absent.
		func hold_repeat_last() -> ScheduleConfig:
			if _handle._allows_code_value(
					&"hold",
					"schedule().hold_repeat_last",
			):
				_handle.missing_policy = MissingInput.REPEAT_LAST
			return self


		## Reopens a lagging consume cursor at [param ticks].
		func resync_ceiling(ticks: int) -> ScheduleConfig:
			if _handle._allows_code_value(
					&"resync_ceiling",
					"schedule().resync_ceiling",
			):
				_handle.max_consume_lag_ticks = maxi(0, ticks)
			return self


		# Applies the scene's raw export before code declarations are admitted.
		func _scene_tier(value: Schedule) -> void:
			_handle._schedule = value

	## Fluent declaration of the state shared with the prediction environment.
	class SensorsConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Samples [param sensor] once before each drive under [param name].
		func sample(name: StringName, sensor: Callable) -> SensorsConfig:
			if not sensor.is_valid():
				push_error(
					"PredictionHandle.sensors().sample: sensor must be a valid "
					+ "Callable.",
				)
				return self
			var sensors: Dictionary = _handle.island_config.get(&"sensors", { })
			sensors[name] = sensor
			_handle.island_config[&"sensors"] = sensors
			return self


		## Removes the sample named [param name].
		func remove(name: StringName) -> SensorsConfig:
			var sensors: Dictionary = _handle.island_config.get(&"sensors", { })
			sensors.erase(name)
			_handle.island_config[&"sensors"] = sensors
			return self

	## Fluent declaration of the solve facts observed after each transition.
	class WitnessConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Samples realized contacts with [param sampler].
		func contacts(sampler: Callable) -> WitnessConfig:
			if not sampler.is_valid():
				push_error(
					"PredictionHandle.witness().contacts: sampler must be a "
					+ "valid Callable.",
				)
				return self
			_handle.witness_config = { &"contacts": sampler }
			return self

	## Fluent declaration of present-time pose transport.
	class TransportConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Accepts pose transport only when [param sweep] reports a clear path.
		func corridor(sweep: Callable) -> TransportConfig:
			if not sweep.is_valid():
				push_error(
					"PredictionHandle.transport().corridor: sweep must be a "
					+ "valid Callable.",
				)
				return self
			_handle.transport_config = { &"corridor": sweep }
			return self

	## Fluent declaration of prediction-island membership and promotion.
	class IslandConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Uses tolerance comparison for this island.
		func approximate() -> IslandConfig:
			_declare()
			_handle.island_config[&"approximate"] = true
			_handle.island_config.erase(&"exact_claim")
			return self


		## Claims exact comparison for an explicit-only island.
		func exact() -> IslandConfig:
			_declare()
			if not _handle.island_config.get(&"producers", []).is_empty():
				push_error(
					"PredictionHandle.island().exact: produced islands are "
					+ "always approximate.",
				)
				return self
			_handle.island_config[&"approximate"] = false
			_handle.island_config[&"exact_claim"] = true
			return self


		## Produces members from the resolved interest scope or [param layer].
		func from_interest(layer: StringName = &"") -> IslandConfig:
			_declare()
			if bool(_handle.island_config.get(&"exact_claim", false)):
				push_error(
					"PredictionHandle.island().from_interest: produced islands "
					+ "cannot claim exact comparison.",
				)
				return self
			var producers: Array = _handle.island_config.get(&"producers", [])
			var producer := { &"kind": &"interest", &"layer": layer }
			if not producers.has(producer):
				producers.append(producer)
			_handle.island_config[&"producers"] = producers
			_handle.island_config[&"approximate"] = true
			return self


		## Adds an explicit runtime [param entity] to the island.
		func add(entity: NetwEntity) -> IslandConfig:
			_declare()
			if not _handle._is_same_scene(entity):
				push_error(
					"PredictionHandle.island().add: islands cannot cross "
					+ "MultiplayerScene boundaries.",
				)
				return self
			var participants: Array = _handle.island_config.get(
				&"participants",
				[],
			)
			if not participants.has(entity):
				participants.append(entity)
			_handle.island_config[&"participants"] = participants
			return self


		## Removes an explicit runtime [param entity] from the island.
		func remove(entity: NetwEntity) -> IslandConfig:
			_declare()
			var participants: Array = _handle.island_config.get(
				&"participants",
				[],
			)
			participants.erase(entity)
			_handle.island_config[&"participants"] = participants
			var fidelity: Dictionary = _handle.island_config.get(&"fidelity", { })
			fidelity.erase(entity)
			_handle.island_config[&"fidelity"] = fidelity
			var predictors: Dictionary = _handle.island_config.get(
				&"command_predictors",
				{ },
			)
			predictors.erase(entity)
			_handle.island_config[&"command_predictors"] = predictors
			return self


		## Promotes the nearest [param count] produced members for simulation.
		func simulate_nearest(count: int) -> IslandConfig:
			_declare()
			_handle.island_config[&"promotion"] = {
				&"kind": &"nearest",
				&"count": maxi(0, count),
			}
			return self


		## Promotes produced members within [param meters] for simulation.
		func simulate_within(meters: float) -> IslandConfig:
			_declare()
			_handle.island_config[&"promotion"] = {
				&"kind": &"within",
				&"meters": maxf(0.0, meters),
			}
			return self


		## Explicitly promotes [param entity] for local simulation. When
		## [param predict_commands] is valid, it is called as
		## [code](participant, tick)[/code] and must return an input [Dictionary].
		func simulate(
				entity: NetwEntity,
				predict_commands: Callable = Callable(),
		) -> IslandConfig:
			add(entity)
			_set_fidelity(entity, Fidelity.SIMULATED)
			if predict_commands.is_valid():
				self.predict_commands(entity, predict_commands)
			return self


		## Sets the substituted command producer for [param entity]. The callable
		## receives [code](participant, tick)[/code] and returns an input
		## [Dictionary]. Leaving it undeclared uses the zero-input COAST policy.
		func predict_commands(
				entity: NetwEntity,
				predictor: Callable,
		) -> IslandConfig:
			add(entity)
			if not predictor.is_valid():
				push_error(
					"PredictionHandle.island().predict_commands: predictor must "
					+ "be a valid Callable.",
				)
				return self
			var predictors: Dictionary = _handle.island_config.get(
				&"command_predictors",
				{ },
			)
			predictors[entity] = predictor
			_handle.island_config[&"command_predictors"] = predictors
			return self


		## Explicitly keeps [param entity] as a displayed proxy.
		func observe(entity: NetwEntity) -> IslandConfig:
			add(entity)
			_set_fidelity(entity, Fidelity.PROXY)
			return self


		## Selects island reconciliation. JOINT remains reserved.
		func reconcile(mode: Reconcile) -> IslandConfig:
			_declare()
			if mode == Reconcile.JOINT:
				push_error(
					"PredictionHandle.island().reconcile: JOINT stays reserved "
					+ "until contact is decisive, error-sensitive, and "
					+ "small-scope.",
				)
				return self
			_handle.island_config[&"reconcile"] = mode
			return self


		# Marks the declaration that admits fingerprint comparison.
		func _declare() -> void:
			if bool(_handle.island_config.get(&"inherited", false)):
				var environment := { }
				for key in [&"sensors", &"epoch"]:
					if _handle.island_config.has(key):
						environment[key] = _handle.island_config[key]
				_handle.island_config = environment
			_handle.island_config[&"declared"] = true
			if not _handle.island_config.has(&"participants"):
				_handle.island_config[&"participants"] = []
			if not _handle.island_config.has(&"reconcile"):
				_handle.island_config[&"reconcile"] = Reconcile.INDEPENDENT


		# Stores one explicit fidelity override.
		func _set_fidelity(entity: NetwEntity, value: Fidelity) -> void:
			var fidelity: Dictionary = _handle.island_config.get(&"fidelity", { })
			if _handle.island_config.get(&"participants", []).has(entity):
				fidelity[entity] = value
			_handle.island_config[&"fidelity"] = fidelity

	## Fluent scene-level prediction-island defaults.
	##
	## [MultiplayerScene.prediction_island] applies this rule to predicted
	## descendants that do not make their own [method PredictionHandle.island]
	## declaration.
	class IslandDefaults:
		extends RefCounted

		var _config: Dictionary


		func _init(config: Dictionary) -> void:
			_config = config
			_declare()


		## Uses tolerance comparison for produced scene membership.
		func approximate() -> IslandDefaults:
			_config[&"approximate"] = true
			_config.erase(&"exact_claim")
			return self


		## Claims exact comparison when the scene rule has no producer.
		func exact() -> IslandDefaults:
			if not _config.get(&"producers", []).is_empty():
				push_error(
					"MultiplayerScene.prediction_island().exact: produced "
					+ "islands are always approximate.",
				)
				return self
			_config[&"approximate"] = false
			_config[&"exact_claim"] = true
			return self


		## Produces members from each entity's resolved interest scope or
		## [param layer].
		func from_interest(layer: StringName = &"") -> IslandDefaults:
			if bool(_config.get(&"exact_claim", false)):
				push_error(
					"MultiplayerScene.prediction_island().from_interest: "
					+ "produced islands cannot claim exact comparison.",
				)
				return self
			var producers: Array = _config.get(&"producers", [])
			var producer := { &"kind": &"interest", &"layer": layer }
			if not producers.has(producer):
				producers.append(producer)
			_config[&"producers"] = producers
			_config[&"approximate"] = true
			return self


		## Promotes the nearest [param count] produced members for simulation.
		func simulate_nearest(count: int) -> IslandDefaults:
			_config[&"promotion"] = {
				&"kind": &"nearest",
				&"count": maxi(0, count),
			}
			return self


		## Promotes produced members within [param meters] for simulation.
		func simulate_within(meters: float) -> IslandDefaults:
			_config[&"promotion"] = {
				&"kind": &"within",
				&"meters": maxf(0.0, meters),
			}
			return self


		## Selects scene-default island reconciliation. JOINT remains reserved.
		func reconcile(mode: Reconcile) -> IslandDefaults:
			if mode == Reconcile.JOINT:
				push_error(
					"MultiplayerScene.prediction_island().reconcile: JOINT "
					+ "stays reserved until contact is decisive, "
					+ "error-sensitive, and small-scope.",
				)
				return self
			_config[&"reconcile"] = mode
			return self


		# Seeds the detached inherited rule with structural defaults.
		func _declare() -> void:
			_config[&"declared"] = true
			_config[&"inherited"] = true
			if not _config.has(&"participants"):
				_config[&"participants"] = []
			if not _config.has(&"reconcile"):
				_config[&"reconcile"] = Reconcile.INDEPENDENT

	## Fluent declaration of per-entity recovery behavior.
	class RecoveryConfig:
		extends RefCounted

		var _handle: PredictionHandle


		func _init(handle: PredictionHandle) -> void:
			_handle = handle


		## Selects the per-entity recovery [param value].
		func policy(value: RecoveryPolicy) -> RecoveryConfig:
			if not _handle._allows_code_value(&"policy", "recovery().policy"):
				return self
			_handle._apply_recovery_policy(value)
			return self


		## Sets the default actionable error [param value].
		func epsilon(value: float) -> RecoveryConfig:
			if _handle._allows_code_value(&"epsilon", "recovery().epsilon"):
				_handle.divergence_epsilon = maxf(0.0, value)
			return self


		## Sets the whole-closure threshold [param value].
		func teleport_threshold(value: float) -> RecoveryConfig:
			if _handle._allows_code_value(
					&"teleport_threshold",
					"recovery().teleport_threshold",
			):
				_handle.teleport_threshold = maxf(0.0, value)
			return self


		## Sets the contact cooldown to [param ticks].
		func cooldown_ticks(ticks: int) -> RecoveryConfig:
			if _handle._allows_code_value(
					&"cooldown_ticks",
					"recovery().cooldown_ticks",
			):
				_handle.collision_cooldown_ticks = maxi(0, ticks)
			return self


		## Selects how SNAP places received state.
		func projection(value: RestoreMode) -> RecoveryConfig:
			if _handle._allows_code_value(
					&"projection",
					"recovery().projection",
			):
				_handle.snap_restore = value
			return self


		## Sets what the entity does when its witness reports contact outside the
		## prediction boundary, then returns this configurator.
		##
		## [constant BreachResponse.DEMOTE] stops speculation at the witnessed
		## transition, follows authority while commands keep flowing, and resumes
		## through a reseed after authority reports a clean witness run.
		## [constant BreachResponse.PREDICT_THROUGH] keeps ordinary reconciliation.
		func on_breach(response: BreachResponse) -> RecoveryConfig:
			_handle.breach_response = response
			return self

	## Lifecycle state of one actionable divergence episode.
	enum EpisodeState {
		## The episode is accepting comparisons and operator evidence.
		OPEN,
		## A verified agreement run retired the episode.
		CLOSED,
		## Bounded recovery evidence was exhausted.
		FALLBACK,
	}

	## Measured outcome of one recovery operator application.
	enum OperatorOutcome {
		## No later aligned comparison has judged the write yet.
		PENDING,
		## A later comparison strictly reduced or closed the aligned error.
		CONTRACTED,
		## The verification window held or grew the aligned error.
		FAILED_TO_CONTRACT,
		## The write introduced a realized boundary absent on authority.
		INTRODUCED_BOUNDARY,
	}

	## Generator status when the causal fork predates retained journal rows.
	const GENERATOR_UNKNOWN_BEYOND_RETENTION := \
			&"UNKNOWN_BEYOND_RETENTION"

	## The simulation step, defaulting to the entity root's
	## [code]_network_tick(delta, tick, is_fresh)[/code]. Set it to route the step
	## through a delegating node. It is a single [Callable], never a fan-out, so
	## exactly one authoritative step runs per entity per tick.
	var simulate: Callable = Callable()

	# Cadence selected through schedule(). Read with resolved_schedule().
	var _schedule: Schedule = Schedule.TICK

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

	## The [enum RecoveryPolicy] declared through [method recovery], or
	## [code]-1[/code] while none has been, in which case
	## [method resolved_recovery_policy] derives one from
	## [member correction_mode].
	var recovery_policy: int = -1

	## Immediate response to an out-of-boundary realized contact.
	##
	## The response is armed only when [member witness_config] declares the
	## observation. [constant BreachResponse.DEMOTE] follows authority through
	## the existing delayed fallback path while the command lane stays live.
	var breach_response: BreachResponse = BreachResponse.PREDICT_THROUGH

	## The island and environment configuration declared through
	## [method island], [method sensors], and
	## [method epoch], keyed by those verbs' own keys plus an internal
	## [code]declared[/code] marker only [method island] sets.
	##
	## Sensors and an epoch are environment attribution, not a claim of exactness,
	## so declaring them alone leaves every transition
	## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN]. Only naming the island
	## participants through [method island] opts an entity into the
	## fingerprint compare. An entity that declares nothing is out of domain on
	## every transition, the absence of a claim rather than a claim of divergence.
	var island_config: Dictionary = { }

	## Realized-transition observation declared through [method witness].
	## An empty value is valid and makes the witness boundary unknown.
	var witness_config: Dictionary = { }

	## Present-time transport declared through [method transport].
	## An empty value keeps the conditional operator disabled.
	var transport_config: Dictionary = { }

	## Ceiling in ticks on the age a [constant RestoreMode.EXTRAPOLATED] restore
	## projects across. The projection advances by the unacked span
	## [member ack_age_ticks]. The prediction governor holds before that span can
	## exceed its structural ceiling, but a linear projection near the ceiling can
	## still land a body far off a curved path. This caps that span the way
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

	## Physics-frame callbacks folded into their tick's existing command record.
	## A growing value means this peer produces frame callbacks faster than the
	## shared network clock advances, but it cannot grow the command queue.
	var authoring_clamped_count: int = 0

	## Prediction passes held because their unacknowledged horizon reached the
	## structural ceiling. Input capture continues while the simulated body holds.
	var speculation_held_count: int = 0

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
	## and every fluent setter refuses a key found here.
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
	## [method island], so an entity that declares no island keeps the
	## tolerance compare it always had.
	var fp_mismatch_count: int = 0

	## The first transition [member fp_mismatch_count] counted, or [code]-1[/code]
	## while none has. This is the transition to inspect, since every later
	## mismatch may be the propagation of this one.
	var first_divergent_transition: int = -1

	## Rows whose pre-state did not chain from the preceding post-state and carried
	## no operator provenance. Any nonzero value names an out-of-transition write.
	var chain_break_count: int = 0

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
	## [constant NetwPredictJournal.Attribution.UNKNOWN] means required evidence
	## was absent. Every other value names the first observed boundary that
	## differed.
	var last_attribution: NetwPredictJournal.Attribution = \
			NetwPredictJournal.Attribution.UNKNOWN

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
	##     if attribution == NetwPredictJournal.Attribution.CLOSURE:
	##         push_warning("transition %d diverged under equal antecedents" % entry)
	## )
	## [/codeblock]
	signal divergence_detected(
		entry: int,
		attribution: NetwPredictJournal.Attribution,
	)

	## Emitted when an actionable settled comparison opens an episode.
	##
	## [param report] has the same schema as [method episode].
	signal episode_opened(report: Dictionary)

	## Emitted after the verified agreement run closes an episode.
	##
	## [param report] has the same schema as [method episode]. It is the final
	## detached record and remains valid after later journal rows evict its
	## generator.
	signal episode_closed(report: Dictionary)

	## Emitted when bounded recovery evidence enters delayed fallback.
	##
	## [param report] has the same schema as [method episode] at quarantine entry.
	## Command capture continues while local speculation is closed.
	signal episode_fallback(report: Dictionary)

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
	var _episode_snapshot: Dictionary = { }
	var _next_episode_id: int = 1
	var _last_closed_episode_id: int = 0
	var _last_closed_transition: int = -1
	var _last_closure_was_fallback: bool = false
	var _fallback_flap_level: int = 0
	var _last_episode_reopen_chain: Array[int] = []
	# Subject instance ids whose local islands currently step this entity.
	var _simulation_subjects: Dictionary[int, Callable] = { }


	# The archetype bundle selected through archetype().
	var _archetype: Archetype = Archetype.NONE


	## Applies the named [param declared] prediction bundle.
	##
	## Scene-declared schedule and recovery facts keep their values. A later
	## fluent declaration may refine any fact the scene did not claim.
	func archetype(declared: Archetype) -> PredictionHandle:
		# The archetype is one fact like any other: a scene that declared it
		# owns it, and a code restatement is refused loudly rather than
		# silently re-deriving the bundle.
		if scene_declared.has(&"archetype"):
			push_error(
				"PredictionHandle.archetype: the archetype is "
				+ "declared on the scene's PredictionComponent and configured "
				+ "from code. One source per fact: keep the scene value or "
				+ "the code call, not both. The code value is ignored.",
			)
			return self
		_archetype = declared
		match declared:
			Archetype.KINEMATIC:
				breach_response = BreachResponse.PREDICT_THROUGH
				if not scene_declared.has(&"tier"):
					_schedule = Schedule.TICK
				if not scene_declared.has(&"hold"):
					missing_policy = MissingInput.STALL
				if not scene_declared.has(&"policy"):
					_apply_recovery_policy(RecoveryPolicy.REBASE_REPLAY)
			Archetype.SOLVER_BODY:
				breach_response = BreachResponse.DEMOTE
				if not scene_declared.has(&"tier"):
					_schedule = Schedule.FRAME
				if not scene_declared.has(&"hold"):
					missing_policy = MissingInput.REPEAT_LAST
				if not scene_declared.has(&"policy"):
					_apply_recovery_policy(RecoveryPolicy.REBASE_RECOVER)
				if not scene_declared.has(&"projection"):
					snap_restore = RestoreMode.EXTRAPOLATED
				if not scene_declared.has(&"teleport_threshold"):
					teleport_threshold = 3.0
			_:
				return self
		return self


	## Returns the fluent schedule declaration for this entity.
	func schedule() -> ScheduleConfig:
		return ScheduleConfig.new(self)


	## Returns the resolved drive cadence.
	func resolved_schedule() -> Schedule:
		return _schedule


	## Returns the declared archetype bundle.
	func resolved_archetype() -> Archetype:
		return _archetype


	## Declares the prediction island: which entities share interactions with this
	## body and how many this peer steps inside its prediction boundary.
	##
	## Membership producers and explicit entities feed one runtime set. The
	## interest producer admits only entities replicated on this peer. Promotion
	## policies select which members become [constant Fidelity.SIMULATED]; every
	## other member remains a witnessed [constant Fidelity.PROXY]. Changes commit
	## at transition boundaries, with distance hysteresis and contact-safe fidelity
	## handoffs.
	## [codeblock]
	## entity.prediction.island() \
	##     .approximate() \
	##     .from_interest() \
	##     .simulate_nearest(1)
	## [/codeblock]
	## A simulated member uses the zero-input COAST command unless
	## [method PredictionHandle.IslandConfig.predict_commands] supplies a command.
	## Each received
	## authority state independently rebases it through the configured projection,
	## so its error is bounded by receive cadence times command error. Its display
	## chases the collidable simulated body. Produced islands are always
	## approximate; [method PredictionHandle.IslandConfig.exact] accepts explicit
	## membership only.
	func island() -> IslandConfig:
		return IslandConfig.new(self)


	## Returns the fluent environment-sensor declaration.
	func sensors() -> SensorsConfig:
		return SensorsConfig.new(self)


	## Returns the fluent realized-witness declaration.
	func witness() -> WitnessConfig:
		return WitnessConfig.new(self)


	## Returns the fluent present-time transport declaration.
	func transport() -> TransportConfig:
		return TransportConfig.new(self)


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
	##     entity.prediction.sensors().sample(&"ground", _sample_ground)
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
	## Bumping [param value] reopens the tolerance window, since two peers cannot
	## have adopted a world change on the same transition. Like
	## [method sensors] this declares the environment, not an island, so
	## it never opts the entity into the fingerprint compare.
	func epoch(value: int) -> PredictionHandle:
		island_config[&"epoch"] = value
		return self


	## Returns the fluent recovery declaration for this entity.
	func recovery() -> RecoveryConfig:
		return RecoveryConfig.new(self)


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


	# Enforces one source for each scene-facing fact.
	func _allows_code_value(key: StringName, verb: String) -> bool:
		if not scene_declared.has(key):
			return true
		push_error(
			(
					"PredictionHandle.%s: '%s' is declared on the scene's "
					+ "PredictionComponent and configured from code. One "
					+ "source per fact: keep the scene value or the code call, "
					+ "not both. The code value is ignored."
			) % [verb, key],
		)
		return false


	# Applies the strategy and refreshes axes that depend on it.
	func _apply_recovery_policy(value: RecoveryPolicy) -> void:
		recovery_policy = value
		correction_mode = _correction_mode_for_policy(value)
		var engine := _engine()
		if engine:
			engine._rewire()


	# True when an explicit member shares the bound entity's scene.
	func _is_same_scene(participant: NetwEntity) -> bool:
		if not participant or not is_instance_valid(participant):
			return false
		var engine := _engine()
		if not engine or not engine._entity:
			return true
		return engine._entity.scene == participant.scene


	# The correction mechanism a policy runs through. A policy names a strategy
	# and the mechanism is how it is carried out, so several policies can share
	# one mechanism without sharing a meaning.
	func _correction_mode_for_policy(policy: RecoveryPolicy) -> CorrectionMode:
		match policy:
			RecoveryPolicy.REBASE_REPLAY:
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
	## [codeblock]
	##             AUTHORITATIVE   SPECULATIVE   DISPLAY
	##   LOCAL     HOST_LOCAL      PREDICT       REMOTE
	##   RECEIVED  CONSUME         (unreachable) REMOTE
	##   PREDICTED (unreachable)   SIMULATE      REMOTE
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
		if source == InputSource.PREDICTED and mode == SimMode.SPECULATIVE:
			return Role.SIMULATE
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


	## Returns the active or most recently retired disturbance episode report.
	##
	## The returned [Dictionary] is detached from engine storage. An empty value
	## means no actionable divergence has opened. The stable report sections are:
	## [codeblock]
	## {
	##     id: int,
	##     generator: {
	##         transition: int,
	##         boundary: NetwPredictJournal.Attribution,
	##         row: Dictionary,
	##     },
	##     operators: Array[{
	##         operator: NetwPredictJournal.Operator,
	##         basis: int,
	##         eligible: bool,
	##         applied: bool,
	##         eligibility: Dictionary,
	##         outcome: OperatorOutcome,
	##         write: Dictionary,
	##     }],
	##     contraction: Array[{transition, meter, agrees, write_id}],
	##     disposition: {
	##         state: EpisodeState,
	##         non_contraction_used: int,
	##         closure_used: int,
	##         closed_transition: int,
	##         fallback_transition: int,
	##         demoted: bool,
	##         breach_transition: int,
	##         breach_witness: Dictionary,
	##         resume_ack_age: int,
	##         quarantine_target: int,
	##         quarantine_clean_run: int,
	##         reseed_transition: int,
	##         aligned_transition: int,
	##     },
	##     reopen_chain: Array[int],
	## }
	## [/codeblock]
	## [code]generator.row[/code] includes the retained journal row and its
	## forensic sidecar. If the causal run predates retention, its status is
	## [constant GENERATOR_UNKNOWN_BEYOND_RETENTION]. Later divergent rows appear
	## in [code]taint[/code], while independent agreeing-pre failures appear in
	## [code]secondary_generators[/code]. The original evidence keys remain in the
	## detached report for capture compatibility. A refused operator has
	## [code]outcome = -1[/code] and an empty [code]write[/code].
	func episode() -> Dictionary:
		var engine := _engine()
		if engine:
			return engine.episode()
		return _episode_report(_episode_snapshot)


	# Projects raw resumable episode state into the stable public report.
	func _episode_report(raw: Dictionary) -> Dictionary:
		if raw.is_empty():
			return { }
		var report := raw.duplicate(true)
		var row: Dictionary = report.get(&"generator_row_copy", { })
		report[&"generator"] = {
			&"transition": int(row.get(
				&"transition",
				report.get(&"opened_transition", -1),
			)),
			&"boundary": int(report.get(
				&"attribution",
				NetwPredictJournal.Attribution.UNKNOWN,
			)),
			&"row": row,
		}
		report[&"operators"] = _episode_operator_attempts(report)
		report[&"contraction"] = (
			report.get(&"comparisons", []) as Array
		).duplicate(true)
		report[&"disposition"] = {
			&"state": int(report.get(&"state", EpisodeState.OPEN)),
			&"non_contraction_used": int(
				report.get(&"non_contraction_used", 0),
			),
			&"closure_used": int(report.get(&"closure_used", 0)),
			&"closed_transition": int(
				report.get(&"closed_transition", -1),
			),
			&"fallback_transition": int(
				report.get(&"fallback_transition", -1),
			),
			&"demoted": bool(report.get(&"demoted", false)),
			&"breach_transition": int(
				report.get(&"breach_transition", -1),
			),
			&"breach_witness": (
				report.get(&"breach_witness", { }) as Dictionary
			).duplicate(true),
			&"resume_ack_age": int(
				report.get(&"resume_ack_age", 0),
			),
			&"quarantine_target": int(
				report.get(&"quarantine_target", 0),
			),
			&"quarantine_clean_run": int(
				report.get(&"quarantine_clean_run", 0),
			),
			&"reseed_transition": int(
				report.get(&"reseed_transition", -1),
			),
			&"aligned_transition": int(
				report.get(&"aligned_transition", -1),
			),
		}
		var chain: Array = report.get(&"reopen_chain", [])
		if chain.is_empty():
			var reopened_from := int(report.get(&"reopened_from", 0))
			if reopened_from > 0:
				chain.append(reopened_from)
			chain.append(int(report.get(&"id", 0)))
		report[&"reopen_chain"] = chain.duplicate()
		return report


	# Joins eligibility decisions to applied writes without losing refusals.
	func _episode_operator_attempts(report: Dictionary) -> Array[Dictionary]:
		var attempts: Array[Dictionary] = []
		var writes: Array = report.get(&"writes", [])
		var used_writes := { }
		for value: Variant in report.get(&"decisions", []):
			var decision := (value as Dictionary).duplicate(true)
			var matched_write := { }
			for index in writes.size():
				if used_writes.has(index):
					continue
				var candidate := writes[index] as Dictionary
				if int(candidate.get(&"operator", -1)) \
						== int(decision.get(&"operator", -2)) \
						and int(candidate.get(&"basis", -1)) \
						== int(decision.get(&"basis", -2)):
					matched_write = candidate.duplicate(true)
					used_writes[index] = true
					break
			decision[&"outcome"] = int(matched_write.get(&"outcome", -1))
			decision[&"write"] = matched_write
			attempts.append(decision)
		for index in writes.size():
			if used_writes.has(index):
				continue
			var write := (writes[index] as Dictionary).duplicate(true)
			attempts.append({
				&"operator": int(write.get(&"operator", -1)),
				&"basis": int(write.get(&"basis", -1)),
				&"eligible": true,
				&"applied": true,
				&"eligibility": { },
				&"outcome": int(write.get(&"outcome", -1)),
				&"write": write,
			})
		return attempts


	# Allocates an episode identity and its reopen link across engine lifetimes.
	func _open_episode_identity(transition: int, flap_window: int) -> Dictionary:
		var reopened_from := 0
		var reopen_chain: Array[int] = []
		if _last_closed_episode_id > 0 \
				and transition >= _last_closed_transition \
				and transition - _last_closed_transition <= flap_window:
			reopened_from = _last_closed_episode_id
			reopen_chain = _last_episode_reopen_chain.duplicate()
			if reopen_chain.is_empty() \
					or reopen_chain.back() != reopened_from:
				reopen_chain = [reopened_from]
			if _last_closure_was_fallback:
				_fallback_flap_level += 1
		else:
			_fallback_flap_level = 0
		reopen_chain.append(_next_episode_id)
		_last_episode_reopen_chain = reopen_chain.duplicate()
		var identity := {
			&"id": _next_episode_id,
			&"reopened_from": reopened_from,
			&"reopen_chain": reopen_chain,
		}
		_next_episode_id += 1
		return identity


	# Keeps episode evidence available when the engine rewires or unregisters.
	func _store_episode(report: Dictionary) -> void:
		_episode_snapshot = report.duplicate(true)


	# Remembers the closure frontier used by the handle-side flap guard.
	func _store_episode_closure(report: Dictionary, transition: int) -> void:
		_store_episode(report)
		_last_closed_episode_id = int(report.get(&"id", 0))
		_last_closed_transition = transition
		_last_closure_was_fallback = int(report.get(&"state", -1)) \
				== EpisodeState.FALLBACK
		if not _last_closure_was_fallback:
			_fallback_flap_level = 0


	# Returns the flap-priced clean run without exceeding its structural cap.
	func _quarantine_target(base: int, cap: int) -> int:
		var multiplier := 1 << mini(_fallback_flap_level, 30)
		return mini(base * multiplier, cap)


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
	##  ┠╴ack_age_ticks (int)      unconfirmed speculative horizon
	##  ┠╴ack_confirmed (int)     highest transition the owner saw acked, or -1
	##  ┠╴frames_dropped_invalid (int)  owner-lane frames dropped whole
	##  ┠╴substituted (int)       transitions authority declared substituted
	##  ┠╴fp_verified (int)       acked transitions the owner could check at all
	##  ┠╴fp_mismatches (int)     of those, the ones that fingerprinted unequal
	##  ┠╴first_divergent_transition (int)   the first of them, or -1
	##  ┠╴client_fp_verified (int)  transitions authority judged the owner's claim on
	##  ┠╴client_mismatches (int)   owner claims that differed from authority
	##  ┠╴island_members (PackedStringArray)  locally replicated produced members
	##  ┖╴simulated_members (PackedStringArray)  members stepped on this peer
	## }
	## [/codeblock]
	## [code]authoring_clamped[/code] counts frame callbacks folded into the
	## current network tick. [code]speculation_held[/code] counts passes held at
	## the structural [code]ack_age_max[/code] ceiling.
	## [code]chain_breaks[/code] counts pre-state rows that no preceding post-state
	## or operator provenance can explain.
	func stats() -> Dictionary:
		var engine := _engine()
		var island_stats := engine._island_stats() if engine else {
			&"island_members": PackedStringArray(),
			&"simulated_members": PackedStringArray(),
		}
		var out := {
			&"schedule": _schedule,
			&"archetype": _archetype,
			&"recovery_policy": resolved_recovery_policy(),
			&"input_source": input_source,
			&"sim_mode": sim_mode,
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
			&"authoring_clamped": authoring_clamped_count,
			&"speculation_held": speculation_held_count,
			&"ack_age_max": _PredictionEngine.ACK_AGE_MAX,
			&"ack_age_ticks": ack_age_ticks,
			&"ack_confirmed": ack_confirmed_transition,
			&"frames_dropped_invalid": frames_dropped_invalid,
			&"substituted": substituted_count,
			&"fp_verified": fp_verified_count,
			&"fp_mismatches": fp_mismatch_count,
			&"first_divergent_transition": first_divergent_transition,
			&"chain_breaks": chain_break_count,
			&"client_fp_verified": client_fp_verified_count,
			&"client_mismatches": client_mismatch_count,
		}
		out.merge(island_stats)
		return out


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


	# Adds or removes one island's local simulation claim and refreshes the axes.
	func _set_simulated_by(
			subject: NetwEntity,
			enabled: bool,
			predictor: Callable = Callable(),
	) -> void:
		if subject == null:
			return
		var subject_id := subject.get_instance_id()
		var changed := false
		if enabled:
			changed = not _simulation_subjects.has(subject_id) \
					or _simulation_subjects[subject_id] != predictor
			_simulation_subjects[subject_id] = predictor
		else:
			changed = _simulation_subjects.erase(subject_id)
		if changed:
			var current := _engine()
			if current:
				current._rewire()


	# Returns the stable first predictor declared by a promoting subject.
	func _predicted_command_callable() -> Callable:
		var ids: Array = _simulation_subjects.keys()
		ids.sort()
		for subject_id: int in ids:
			var predictor := _simulation_subjects[subject_id] as Callable
			if predictor.is_valid():
				return predictor
		return Callable()


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
#     [version u8][ack_of_acks varint]  codec version and highest seen ack
#     [epoch u8][base varint][count u8]
#     [transition bits]        zigzag label delta << 1 | fresh, per transition
#     [payload section]        one windowed payload per FRESH transition
#     [fp section]             count u8, then one record per closed transition
#       [mask u8][pre][post][environment][topology][witness] as u32
#       [three pre-family u32][three post-family u32][raw u32 when masked]
#
#   ACK (authority -> owner, unreliable)
#     [version u8][epoch u8][base varint][count u8]
#     per transition: mask, pre, command, environment, post, topology, witness,
#                     three pre families, three post families, optional raw,
#                     authority witness-class bits, and flags
#     one MTU-safe oldest prefix per send; ack-of-acks advances later chunks
class _PredictFrames extends RefCounted:
	const WIRE_VERSION := 5
	# Set on an ack record when authority ran a command the owner never authored.
	const ACK_SUBSTITUTED := 1 << 0

	# Set on an ack record when a later truth replaced an earlier verdict.
	const ACK_SUPERSEDED := 1 << 1
	const ACK_WITNESS_SHIFT := 2
	const ACK_WITNESS_MASK := 0x1C
	# Twenty-one worst-case raw records keep the framed unreliable payload below
	# the carrier's conservative 1200-byte aggregation threshold.
	const ACK_RECORD_MAX := 21
	const ACK_FRAME_BYTES_MAX := 1150

	const _MAX_RUN := 255
	const _U32 := 0xFFFFFFFF


	# Writes one COMMAND frame. [param transitions] are contiguous
	# {index, label, fresh} rows. [param payloads] holds one value array per fresh
	# row in the same order, encoded through [param quantizers] and [param types].
	# The fingerprint section carries the owner's post-state digest beside the
	# environment it ran against, the same pair the ACK frame carries in the other
	# direction and for the same reason: a disagreement neither peer can charge to
	# the command or to the world is the one charged to the closure, and
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
			pre_fps: PackedInt32Array = PackedInt32Array(),
			pre_family_fps: PackedInt32Array = PackedInt32Array(),
			post_family_fps: PackedInt32Array = PackedInt32Array(),
			topo_fps: PackedInt32Array = PackedInt32Array(),
			witness_fps: PackedInt32Array = PackedInt32Array(),
			raw_fps: PackedInt32Array = PackedInt32Array(),
			evidence_masks: PackedByteArray = PackedByteArray(),
	) -> PackedByteArray:
		var w := NetwBitBuffer.Writer.new()
		w.put_aligned_u8(WIRE_VERSION)
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
		var fp_count := mini(
			count,
			_claim_count(
				pre_fps,
				post_fps,
				e_digests,
				pre_family_fps,
				post_family_fps,
				topo_fps,
				witness_fps,
				raw_fps,
				evidence_masks,
			),
		)
		w.put_aligned_u8(fp_count)
		for i in fp_count:
			w.put_aligned_u8(evidence_masks[i])
			w.put_aligned_u32(pre_fps[i] & _U32)
			w.put_aligned_u32(post_fps[i] & _U32)
			w.put_aligned_u32(e_digests[i] & _U32)
			w.put_aligned_u32(topo_fps[i] & _U32)
			w.put_aligned_u32(witness_fps[i] & _U32)
			for family in 3:
				w.put_aligned_u32(pre_family_fps[i * 3 + family] & _U32)
			for family in 3:
				w.put_aligned_u32(post_family_fps[i * 3 + family] & _U32)
			if evidence_masks[i] & NetwPredictJournal.EVIDENCE_RAW:
				w.put_aligned_u32(raw_fps[i] & _U32)
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
		if r.get_aligned_u8() != WIRE_VERSION:
			return { }
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
		if fp_count > count:
			return { }
		var pre_fps := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var pre_family_fps := PackedInt32Array()
		var post_family_fps := PackedInt32Array()
		var topo_fps := PackedInt32Array()
		var witness_fps := PackedInt32Array()
		var raw_fps := PackedInt32Array()
		var evidence_masks := PackedByteArray()
		for i in fp_count:
			if r.remaining_bytes() < 45:
				return { }
			var evidence_mask := r.get_aligned_u8()
			evidence_masks.append(evidence_mask)
			pre_fps.append(_to_signed(r.get_aligned_u32()))
			post_fps.append(_to_signed(r.get_aligned_u32()))
			e_digests.append(_to_signed(r.get_aligned_u32()))
			topo_fps.append(_to_signed(r.get_aligned_u32()))
			witness_fps.append(_to_signed(r.get_aligned_u32()))
			for family in 3:
				pre_family_fps.append(_to_signed(r.get_aligned_u32()))
			for family in 3:
				post_family_fps.append(_to_signed(r.get_aligned_u32()))
			if evidence_mask & NetwPredictJournal.EVIDENCE_RAW:
				if r.remaining_bytes() < 4:
					return { }
				raw_fps.append(_to_signed(r.get_aligned_u32()))
			else:
				raw_fps.append(0)
		return {
			"epoch": epoch,
			"ack_of_acks": ack_of_acks,
			"transitions": transitions,
			"payloads": payloads,
			"pre_fps": pre_fps,
			"post_fps": post_fps,
			"e_digests": e_digests,
			"pre_family_fps": pre_family_fps,
			"post_family_fps": post_family_fps,
			"topo_fps": topo_fps,
			"witness_fps": witness_fps,
			"raw_fps": raw_fps,
			"evidence_masks": evidence_masks,
		}


	# Writes one ACK frame from parallel columns beginning at [param base]. The
	# environment digest rides beside the two fingerprints because a divergence
	# the owner cannot charge to the command or to the environment is charged to
	# the closure, and that last step is only honest once the first two have
	# been tested against authority's own values rather than assumed.
	static func encode_ack(
			epoch: int,
			base: int,
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
			flags: PackedByteArray,
	) -> PackedByteArray:
		var w := NetwBitBuffer.Writer.new()
		var count := mini(
			mini(
				_claim_count(
					pre_fps,
					post_fps,
					e_digests,
					pre_family_fps,
					post_family_fps,
					topo_fps,
					witness_fps,
					raw_fps,
					evidence_masks,
				),
				ACK_RECORD_MAX,
			),
			mini(c_hashes.size(), flags.size()),
		)
		w.put_aligned_u8(WIRE_VERSION)
		w.put_aligned_u8(epoch & 0xFF)
		NetwCodec.put_varint(w, base)
		w.put_aligned_u8(count)
		for i in count:
			w.put_aligned_u8(evidence_masks[i])
			w.put_aligned_u32(pre_fps[i] & _U32)
			w.put_aligned_u32(c_hashes[i] & _U32)
			w.put_aligned_u32(e_digests[i] & _U32)
			w.put_aligned_u32(post_fps[i] & _U32)
			w.put_aligned_u32(topo_fps[i] & _U32)
			w.put_aligned_u32(witness_fps[i] & _U32)
			for family in 3:
				w.put_aligned_u32(pre_family_fps[i * 3 + family] & _U32)
			for family in 3:
				w.put_aligned_u32(post_family_fps[i * 3 + family] & _U32)
			if evidence_masks[i] & NetwPredictJournal.EVIDENCE_RAW:
				w.put_aligned_u32(raw_fps[i] & _U32)
			w.put_aligned_u8(flags[i])
		return w.to_bytes()


	# Reads one ACK frame into {base, c_hashes, e_digests, post_fps, flags} as
	# parallel packed columns, the one hot-path crossing this family never turns
	# into dictionaries.
	static func decode_ack(payload: PackedByteArray) -> Dictionary:
		if payload.size() < 4:
			return { }
		var r := NetwBitBuffer.Reader.new(payload)
		if r.get_aligned_u8() != WIRE_VERSION:
			return { }
		var epoch := r.get_aligned_u8()
		var base := NetwCodec.get_safe_varint(r)
		if base < 0:
			return { }
		var count := r.get_aligned_u8()
		var pre_fps := PackedInt32Array()
		var c_hashes := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var pre_family_fps := PackedInt32Array()
		var post_family_fps := PackedInt32Array()
		var topo_fps := PackedInt32Array()
		var witness_fps := PackedInt32Array()
		var raw_fps := PackedInt32Array()
		var evidence_masks := PackedByteArray()
		var witness_class_bits := PackedByteArray()
		var flags := PackedByteArray()
		for i in count:
			if r.remaining_bytes() < 50:
				return { }
			var evidence_mask := r.get_aligned_u8()
			evidence_masks.append(evidence_mask)
			pre_fps.append(_to_signed(r.get_aligned_u32()))
			c_hashes.append(_to_signed(r.get_aligned_u32()))
			e_digests.append(_to_signed(r.get_aligned_u32()))
			post_fps.append(_to_signed(r.get_aligned_u32()))
			topo_fps.append(_to_signed(r.get_aligned_u32()))
			witness_fps.append(_to_signed(r.get_aligned_u32()))
			for family in 3:
				pre_family_fps.append(_to_signed(r.get_aligned_u32()))
			for family in 3:
				post_family_fps.append(_to_signed(r.get_aligned_u32()))
			if evidence_mask & NetwPredictJournal.EVIDENCE_RAW:
				if r.remaining_bytes() < 5:
					return { }
				raw_fps.append(_to_signed(r.get_aligned_u32()))
			else:
				raw_fps.append(0)
			var ack_flags := r.get_aligned_u8()
			witness_class_bits.append(
				(ack_flags & ACK_WITNESS_MASK) >> ACK_WITNESS_SHIFT,
			)
			flags.append(ack_flags & (0xFF ^ ACK_WITNESS_MASK))
		return {
			"epoch": epoch,
			"base": base,
			"pre_fps": pre_fps,
			"c_hashes": c_hashes,
			"e_digests": e_digests,
			"post_fps": post_fps,
			"pre_family_fps": pre_family_fps,
			"post_family_fps": post_family_fps,
			"topo_fps": topo_fps,
			"witness_fps": witness_fps,
			"raw_fps": raw_fps,
			"evidence_masks": evidence_masks,
			"witness_class_bits": witness_class_bits,
			"flags": flags,
		}


	# Fingerprints cross as u32 and are held as signed 32-bit, the width the journal
	# column carries, so a value never changes representation between the two.
	static func _to_signed(value: int) -> int:
		return value - 0x100000000 if value >= 0x80000000 else value


	static func _claim_count(
			pre_fps: PackedInt32Array,
			post_fps: PackedInt32Array,
			e_digests: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
	) -> int:
		return mini(
			mini(
				mini(pre_fps.size(), mini(post_fps.size(), e_digests.size())),
				mini(
					topo_fps.size(),
					mini(witness_fps.size(), raw_fps.size()),
				),
			),
			mini(
				floori(pre_family_fps.size() / 3.0),
				mini(
					floori(post_family_fps.size() / 3.0),
					mini(evidence_masks.size(), _MAX_RUN),
				),
			),
		)


	static func _encode_zigzag(value: int) -> int:
		return (value << 1) if value >= 0 else ((-value << 1) - 1)


	static func _decode_zigzag(value: int) -> int:
		return (value >> 1) if value & 1 == 0 else -((value >> 1) + 1)


class _PredictionEngine extends RefCounted:
	const TAPE_HISTORY_LIMIT := 256
	# The owner may speculate through at most one quarter of the history ring.
	# At the standard 60 Hz network rate this is about one second, leaving three
	# quarters of the ring for acknowledgement and recovery evidence.
	const ACK_AGE_MAX := TAPE_HISTORY_LIMIT >> 2
	# Acknowledgement records repeated per send. The lane is unreliable, so it
	# re-sends what the owner has not confirmed, but an owner whose confirmation
	# never arrives must not grow the frame without bound. Its own
	# acknowledgement floor advances the moment any frame lands.
	const ACK_WINDOW_MAX := 64
	const RAW_FP_ENV := "NETW_PREDICT_RAW_FP"
	const STATE_FAMILY_POSE := 0
	const STATE_FAMILY_MOMENTUM := 1
	const STATE_FAMILY_CONTROLLER := 2
	# Consecutive non-shrinking recoveries that promote the next one to a full
	# closure. Fixed rather than configurable: convergence is a guarantee the
	# kernel owes at every configuration, not a knob whose wrong value can
	# forfeit it.
	const ESCALATE_NONSHRINK := 3
	# A closure and operator outcome must survive a full acknowledgement horizon.
	const EPISODE_CLOSE_RUN := ACK_WINDOW_MAX
	const EPISODE_FLAP_WINDOW := ACK_WINDOW_MAX
	const EPISODE_NON_CONTRACTION_BUDGET := 4
	const EPISODE_FULL_CLOSURE_BUDGET := 2
	# A clean proof spans two measured acknowledgement ages, floored at three
	# distinct authority rows and capped below the evidence ring.
	const RESUME_ACK_MULTIPLIER := 2
	const RESUME_RUN_MIN := 3
	const QUARANTINE_RUN_CAP := ACK_WINDOW_MAX * 4

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
	# State field -> family index: pose, momentum, or controller and latches.
	var _state_family_of: Dictionary[StringName, int] = { }
	# State fields the next transition may read. Derived and cosmetic values do
	# not participate in transport eligibility.
	var _causal_fields: Dictionary[StringName, bool] = { }

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
	var _last_frame_transition_tick: int = -1
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
	# Freshest transition authority proved through either the ack or state lane.
	var _latest_authority_ack: int = -1
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
	var _last_recovery_direction := StringName()
	var _last_recovery_sign: int = 0
	var _last_recovery_divergence: float = -1.0
	var _escalate_next: bool = false
	# The actionable disturbance record. Its detached copy lives on the handle.
	var _episode: Dictionary = { }
	# Transition sidecars are bounded with the journal and copied on generator pin.
	var _forensic_sidecars: Dictionary = { }
	# Fallback closes speculation while retaining local input authoring.
	var _fallback_latched: bool = false
	var _quarantine_stream_reconstructed: bool = false
	var _quarantine_clean_run: int = 0
	var _quarantine_target: int = RESUME_RUN_MIN
	var _quarantine_last_tick: int = -1
	var _quarantine_last_basis: int = -1
	var _quarantine_pending_states: Dictionary[int, Dictionary] = { }
	var _reseed_align_pending: bool = false
	var _reseed_epoch_confirmed: bool = false
	var _reseed_ignore_through: int = -1
	# The last out-of-transition body write, stamped onto the next new row.
	var _pending_provenance: Dictionary = { }
	var _next_operator_write_id: int = 1
	# Witness state spans consecutive solves but never changes the simulation.
	var _previous_witness_sleeping: bool = false
	var _has_previous_witness: bool = false
	var _invalid_witness_reported: bool = false
	var _invalid_command_predictor_reported: bool = false
	var _raw_fp_enabled: bool = false
	# Conditional operators wait for the ack lane to align the basis witness.
	# The state survives only until that verdict or a newer state supersedes it.
	var _witness_verdicts: Dictionary[int, int] = { }
	var _authority_witness_classes: Dictionary[int, int] = { }
	var _deferred_operator_states: Dictionary[int, Dictionary] = { }
	var _operator_deferred_basis: int = -1

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
	# Produced membership and fidelity are committed only at transition boundaries.
	var _island_members: Array[NetwEntity] = []
	var _simulated_members: Dictionary[NetwEntity, bool] = { }
	var _realized_contact_entities: Dictionary[NetwEntity, bool] = { }

	# What the owner claims it reached, keyed by transition, held on authority
	# until authority has run that transition itself. The owner ships a claim
	# ahead of the consume cursor, so a verdict taken on arrival would judge a row
	# authority has not written yet.
	#   { transition: int -> { pre_fp, post_fp, e_digest, family columns } }
	var _owner_claims: Dictionary = { }


	# Binds to the interface and entity, following control transfer and reparent so
	# the role re-resolves in place.
	func _attach(iface: NetwLagCompensationInterface, entity: NetwEntity) -> void:
		_iface_ref = weakref(iface)
		_entity = entity
		_handle = entity.prediction
		_apply_scene_island_defaults()
		_episode = _handle._episode_snapshot.duplicate(true)
		if not entity.control_changed.is_connected(_on_control_changed):
			entity.control_changed.connect(_on_control_changed)
		if not entity.reparented.is_connected(_on_reparented):
			entity.reparented.connect(_on_reparented)
		_rewire()


	# Leaves the loop and restores the set-handle hooks, keeping the handle's config
	# and counters so a re-registration resumes.
	func _release() -> void:
		_clear_island_promotions()
		_handle._simulation_subjects.clear()
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


	func _island_stats() -> Dictionary:
		var members := PackedStringArray()
		for member: NetwEntity in _island_members:
			members.append(String(member.entity_id))
		members.sort()
		var simulated := PackedStringArray()
		for member: NetwEntity in _simulated_members:
			simulated.append(String(member.entity_id))
		simulated.sort()
		return {
			&"island_members": members,
			&"simulated_members": simulated,
		}


	func network_tick(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		if _handle._schedule == PredictionHandle.Schedule.TICK:
			simulate_tick(timing)
			return
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_author_tick(timing.tick)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_author_tick(timing.tick)
			PredictionHandle.Role.REMOTE:
				if _fallback_latched:
					_fallback_author_tick(timing.tick)
			PredictionHandle.Role.SIMULATE:
				pass


	func simulate_tick(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		match _role:
			PredictionHandle.Role.PREDICT:
				_predict_step(timing.delta, timing.tick)
			PredictionHandle.Role.CONSUME:
				_consume_step(timing.delta, timing.tick)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_step(timing.delta, timing.tick)
			PredictionHandle.Role.REMOTE:
				if _fallback_latched:
					_fallback_author_step(timing.tick)
			PredictionHandle.Role.SIMULATE:
				_simulated_step(timing.delta, timing.tick)


	func simulate_frame(timing: PredictTiming) -> void:
		_adopt_timing(timing)
		if _handle._schedule != PredictionHandle.Schedule.FRAME:
			return
		match _role:
			PredictionHandle.Role.PREDICT:
				if timing.tick <= _last_frame_transition_tick:
					_handle.authoring_clamped_count += 1
					_send_command_frame()
					return
				_predict_frame_step(timing)
			PredictionHandle.Role.CONSUME:
				_consume_frame_step(timing)
			PredictionHandle.Role.HOST_LOCAL:
				_host_local_frame_step(timing)
			PredictionHandle.Role.REMOTE:
				if _fallback_latched:
					if timing.tick <= _last_frame_transition_tick:
						_handle.authoring_clamped_count += 1
						_send_command_frame()
						return
					_fallback_author_frame_step(timing)
			PredictionHandle.Role.SIMULATE:
				_simulated_frame_step(timing)


	# Commits produced membership and promotion before a schedule-tier transition.
	func prepare_island(schedule: PredictionHandle.Schedule) -> void:
		if _handle._schedule != schedule or _role not in [
			PredictionHandle.Role.PREDICT,
			PredictionHandle.Role.CONSUME,
			PredictionHandle.Role.HOST_LOCAL,
		]:
			return
		_refresh_island_membership(_role == PredictionHandle.Role.PREDICT)


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
	## Once the frame governor admits a pass, a frame with no newer input than the
	## last one driven repeats that input under the label it already had rather
	## than inventing one. The governor normally folds such surplus callbacks
	## before this decision.
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
	## it no longer decides one. An unjudged transition clears it, since reporting
	## a previous transition's errors as this one's would be worse than reporting
	## none.
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
		field_sink.clear()
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


	## Returns the integral aligned error meter for one settled comparison.
	##
	## [param field_sink] maps causal field names to canonical error magnitudes.
	## [param tolerances] maps the same names to their fixed zero-region bound.
	## A non-positive bound is an exact field: zero agrees and any error measures
	## one. Infinite or missing-field errors saturate rather than overflowing.
	static func measure(field_sink: Dictionary, tolerances: Dictionary) -> int:
		var meter := 0
		for field: StringName in tolerances:
			var error := float(field_sink.get(field, INF))
			if not is_finite(error):
				return 0x7FFFFFFF
			var tolerance := maxf(0.0, float(tolerances.get(field, 0.0)))
			if tolerance <= 0.0:
				meter = maxi(meter, 1 if error > 0.0 else 0)
				continue
			meter = maxi(
				meter,
				ceili(maxf(error - tolerance, 0.0) / tolerance),
			)
		return meter


	## Charges one divergence to the antecedent that differed, returning the
	## [enum NetwPredictJournal.Attribution] it earns.
	##
	## The boundaries are tested in causal order. Missing required peer evidence
	## returns [constant NetwPredictJournal.Attribution.UNKNOWN], never agreement.
	static func attribute(
			pre_equal: bool,
			command_equal: bool,
			environment_equal: bool,
			topology_equal: bool = true,
			raw_equal: bool = true,
			witness_equal: bool = true,
			local_evidence: int = NetwPredictJournal.EVIDENCE_WITNESS,
			peer_evidence: int = NetwPredictJournal.EVIDENCE_WITNESS,
			evidence_complete: bool = true,
	) -> NetwPredictJournal.Attribution:
		if not evidence_complete:
			return NetwPredictJournal.Attribution.UNKNOWN
		if not pre_equal:
			return NetwPredictJournal.Attribution.PRE_STATE
		if not command_equal:
			return NetwPredictJournal.Attribution.COMMAND
		if not environment_equal:
			return NetwPredictJournal.Attribution.ENVIRONMENT
		if not topology_equal:
			return NetwPredictJournal.Attribution.TOPOLOGY
		var both_raw := bool(
			local_evidence & NetwPredictJournal.EVIDENCE_RAW,
		) and bool(peer_evidence & NetwPredictJournal.EVIDENCE_RAW)
		if both_raw and not raw_equal:
			return NetwPredictJournal.Attribution.EXECUTION
		var both_witness := bool(
			local_evidence & NetwPredictJournal.EVIDENCE_WITNESS,
		) and bool(peer_evidence & NetwPredictJournal.EVIDENCE_WITNESS)
		if not both_witness:
			return NetwPredictJournal.Attribution.UNKNOWN
		if not witness_equal:
			return NetwPredictJournal.Attribution.CONTACT
		return NetwPredictJournal.Attribution.CLOSURE


	## Fingerprints an uncanonicalized state payload in sorted field order.
	static func raw_state_fingerprint(payload: Dictionary) -> int:
		var keys := payload.keys()
		keys.sort()
		var bytes := PackedByteArray()
		for key: StringName in keys:
			bytes.append_array(var_to_bytes(key))
			bytes.append_array(var_to_bytes(payload[key]))
		return NetwPredictJournal.fnv1a(bytes)


	## Fingerprints a sorted set of execution facts.
	static func fact_fingerprint(facts: Dictionary) -> int:
		var keys := facts.keys()
		keys.sort()
		var bytes := PackedByteArray()
		for key: StringName in keys:
			bytes.append_array(var_to_bytes(key))
			bytes.append_array(var_to_bytes(facts[key]))
		return NetwPredictJournal.fnv1a(bytes)


	## Buckets a realized contact count as zero, one, two, three, or four-plus.
	static func contact_count_bucket(count: int) -> int:
		return clampi(count, 0, 4)


	## Returns the first state family whose fingerprint differs, or
	## [constant NetwPredictJournal.StateFamily.NONE] when evidence is incomplete
	## or every family agrees.
	static func differing_family(
			local: PackedInt32Array,
			peer: PackedInt32Array,
	) -> NetwPredictJournal.StateFamily:
		if local.size() < 3 or peer.size() < 3:
			return NetwPredictJournal.StateFamily.NONE
		for i in 3:
			if local[i] != peer[i]:
				return (i + 1) as NetwPredictJournal.StateFamily
		return NetwPredictJournal.StateFamily.NONE


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
	## SNAP, out of domain     ──> restore all (partiality is an in-domain
	##                             refinement)
	## SNAP, suppressed        ──> skip
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
	## [method NetwScriptModel.PropertyConfig.teleport_only] declares.
	## [param converge_rules] is the middle answer between withholding a
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
	## [constant NetwPredictJournal.Attribution.UNKNOWN] while an open
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
			domain: NetwPredictJournal.Domain,
			attribution: NetwPredictJournal.Attribution,
			contact_window: bool,
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
		# Partiality is an in-domain refinement. A divergence outside the
		# declared domain, or one nobody could charge while a contact was still
		# disturbing the bodies, gives no ground to decide which fields are safe
		# to leave predicted, so the recovery re-bases the whole closure.
		if domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN \
				or (attribution == NetwPredictJournal.Attribution.UNKNOWN
						and contact_window):
			return {
				&"restore": restore,
				&"write": restore,
				&"teleport": false,
				&"skip": false,
			}
		# A settling body and a diverging one look alike for a few ticks after a
		# contact. This pause applies only after the domain contract has earned
		# partial recovery. An out-of-domain divergence must still close the whole
		# state it cannot reason about.
		if suppressed:
			return {
				&"restore": { },
				&"write": { },
				&"teleport": false,
				&"skip": true,
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


	## Identifies a delta's dominant axis and its sign.
	##
	## A sign flip is evidence of overshoot only when two recoveries answer the
	## same field on the same axis. The returned [code]key[/code] carries both.
	## A type with no signed axis returns an empty key and a zero sign.
	static func delta_direction(field: StringName, delta: Variant) -> Dictionary:
		var axis := -1
		var component := 0.0
		match typeof(delta):
			TYPE_FLOAT:
				axis = 0
				component = delta as float
			TYPE_VECTOR2:
				var v2 := delta as Vector2
				axis = 0 if absf(v2.x) >= absf(v2.y) else 1
				component = v2[axis]
			TYPE_VECTOR3:
				var v3 := delta as Vector3
				axis = 0
				if absf(v3.y) > absf(v3[axis]):
					axis = 1
				if absf(v3.z) > absf(v3[axis]):
					axis = 2
				component = v3[axis]
		if axis < 0:
			return { &"key": StringName(), &"sign": 0 }
		return {
			&"key": StringName("%s:%d" % [field, axis]),
			&"sign": int(signf(component)),
		}


	## Drops from [param projection] every field whose derivative channel is
	## itself diverged, returning the pairs a restore may still project along.
	##
	## Projecting a field forward along a channel authority disagrees about
	## launches the restore along the wrong line, which widens the divergence
	## the recovery answers. The guard is per field: one diverged channel costs
	## its own field the projection and no other, so a converged channel keeps
	## the landing accuracy projection buys. A channel is diverged when its
	## entry in [param field_divergence] would introduce more projected error than
	## the destination field tolerates over the restore span. This is independent
	## from the derivative channel's correction-trigger epsilon.
	static func guard_projection(
			projection: Dictionary,
			field_divergence: Dictionary,
			epsilon: float,
			overrides: Dictionary,
			ack_age_ticks: int,
			max_restore_ticks: int,
			tick_delta: float,
	) -> Dictionary:
		if projection.is_empty() or field_divergence.is_empty():
			return projection
		var span := float(clampi(ack_age_ticks, 0, max_restore_ticks)) \
				* tick_delta
		var out := { }
		for field: StringName in projection:
			var channel: StringName = projection[field]
			var limit := float(overrides.get(field, epsilon))
			var projected_error := float(
				field_divergence.get(channel, 0.0),
			) * span
			if projected_error < limit:
				out[field] = channel
		return out


	## Decides whether one transition is entitled to reproduce exactly, returning
	## the [enum NetwPredictJournal.Domain] it earns.
	##
	## Exactness is claimed, never assumed. An entity that never declared an
	## island through
	## [method NetwLagCompensationInterface.PredictionHandle.island]
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


	## Composes an aligned historical pose error onto the live pose.
	##
	## [param predicted] and [param authority] describe the settled basis.
	## [param current] is the newer live state. Keys in [param pose_fields] use
	## translation, except keys in [param angles], which use the shortest arc.
	## The result carries [code]restore[/code], the partial payload to stage,
	## [code]delta[/code], the transported error, and [code]valid[/code]. No
	## causal non-pose field is written.
	static func transport(
			predicted: Dictionary,
			authority: Dictionary,
			current: Dictionary,
			pose_fields: Dictionary,
			angles: Dictionary = { },
	) -> Dictionary:
		var restore: Dictionary = { }
		var deltas: Dictionary = { }
		for field: StringName in pose_fields:
			if not predicted.has(field) or not authority.has(field) \
					or not current.has(field):
				return {
					&"restore": { },
					&"delta": { },
					&"valid": false,
				}
			var delta: Variant = _pose_delta(
				authority[field],
				predicted[field],
				angles.has(field),
			)
			if delta == null:
				return {
					&"restore": { },
					&"delta": { },
					&"valid": false,
				}
			deltas[field] = delta
			restore[field] = _pose_sum(current[field], delta)
		return {
			&"restore": restore,
			&"delta": deltas,
			&"valid": not restore.is_empty(),
		}


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
		if _handle._schedule != PredictionHandle.Schedule.FRAME:
			return
		if _role != PredictionHandle.Role.PREDICT:
			return
		var state := _capture()
		if _last_driven_entry_index > _last_recorded_entry_index:
			_entry_history.record_state(_last_driven_entry_index + 1, state)
			_close_journal_row(_last_driven_entry_index, state)
			_last_recorded_entry_index = _last_driven_entry_index
		if _last_driven_input_tick > _last_recorded_input_tick:
			_timeline.record_state(_last_driven_input_tick + 1, state)
			_last_recorded_input_tick = _last_driven_input_tick


	func uses_schedule(schedule: PredictionHandle.Schedule) -> bool:
		return _handle._schedule == schedule


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
		if _handle._schedule == PredictionHandle.Schedule.TICK:
			return _timeline.state_at(entry_index + 1) if _timeline else { }
		return _entry_history.state_at(entry_index + 1)


	func journal() -> NetwPredictJournal:
		return _journal


	## Returns a detached copy of the engine's episode evidence.
	func episode() -> Dictionary:
		return _handle._episode_report(_episode)


	# Copies the episode onto the handle after every evidence mutation.
	func _sync_episode() -> void:
		if _handle and not _episode.is_empty():
			_handle._store_episode(_episode)


	# Opens one behavioral episode at the first actionable settled comparison.
	func _open_episode(
			transition: int,
			attribution: NetwPredictJournal.Attribution,
	) -> void:
		var identity := _handle._open_episode_identity(
			transition,
			EPISODE_FLAP_WINDOW,
		)
		_episode = {
			&"id": int(identity[&"id"]),
			&"opened_transition": transition,
			&"generator_row_copy": _pin_generator(transition),
			&"attribution": attribution,
			&"writes": [],
			&"non_contraction_used": 0,
			&"closure_used": 0,
			&"state": PredictionHandle.EpisodeState.OPEN,
			&"reopened_from": int(identity[&"reopened_from"]),
			&"reopen_chain": (
				identity[&"reopen_chain"] as Array
			).duplicate(),
			&"taint": [],
			&"secondary_generators": [],
			&"comparisons": [],
			&"decisions": [],
			&"transport_decided": false,
			&"dissipate_decided": false,
			&"agreement_run": 0,
			&"last_comparison_transition": -1,
			&"agreement_write_id": 0,
			&"last_write_id": 0,
		}
		_next_operator_write_id = 1
		_sync_episode()
		_handle.episode_opened.emit(_handle._episode_report(_episode))


	# Pins the earliest retained row in the actionable out-of-domain run.
	func _pin_generator(crossing: int) -> Dictionary:
		var transitions := _journal.transitions()
		var index := transitions.find(crossing)
		if index < 0:
			return {
				&"status": PredictionHandle.GENERATOR_UNKNOWN_BEYOND_RETENTION,
			}
		var row := _journal.row_at(crossing)
		if _is_actionable_generator_row(row):
			while index > 0:
				var previous := _journal.row_at(int(transitions[index - 1]))
				if not _is_actionable_generator_row(previous):
					break
				index -= 1
				row = previous
		if index == 0 and transitions.size() >= TAPE_HISTORY_LIMIT \
				and _is_actionable_generator_row(row):
			return {
				&"status": PredictionHandle.GENERATOR_UNKNOWN_BEYOND_RETENTION,
				&"retained_anchor": _pin_row(int(transitions[0])),
			}
		return _pin_row(int(row.get(&"transition", crossing)))


	# True when a mismatch was actionable under the tolerance comparison.
	func _is_actionable_generator_row(row: Dictionary) -> bool:
		return bool(
			int(row.get(&"flags", 0)) & NetwPredictJournal.ROW_DIVERGENT,
		) \
				and int(row.get(&"domain", NetwPredictJournal.Domain.IN_DOMAIN)) \
				== NetwPredictJournal.Domain.OUT_OF_DOMAIN


	# Copies one row and its debug-only forensic sidecar out of bounded storage.
	func _pin_row(transition: int) -> Dictionary:
		var row := _journal.row_at(transition).duplicate(true)
		if row.is_empty():
			return {
				&"status": PredictionHandle.GENERATOR_UNKNOWN_BEYOND_RETENTION,
			}
		row[&"sidecar"] = _forensic_sidecars.get(
			transition,
			{ },
		).duplicate(true)
		return row


	# Holds one state until the ack lane supplies its aligned witness verdict.
	func _defer_operator_for_witness(
			recv_tick: int,
			basis: int,
			payload: Dictionary,
	) -> bool:
		if _episode.is_empty() or _witness_verdicts.has(basis):
			return false
		var transport_waits := not _handle.transport_config.is_empty() \
				and not bool(_episode.get(&"transport_decided", false))
		var dissipate_waits := _has_dissipate_fields() \
				and not bool(_episode.get(&"dissipate_decided", false)) \
				and _handle.resolved_recovery_policy() \
				!= PredictionHandle.RecoveryPolicy.OBSERVE
		if not transport_waits and not dissipate_waits:
			return false
		if _operator_deferred_basis >= 0 \
				and _operator_deferred_basis != basis:
			_record_unavailable_operator(
				_operator_deferred_basis,
				transport_waits,
				dissipate_waits,
			)
			_deferred_operator_states.erase(_operator_deferred_basis)
			_operator_deferred_basis = -1
			return false
		_deferred_operator_states[basis] = {
			&"recv_tick": recv_tick,
			&"payload": payload.duplicate(true),
		}
		_operator_deferred_basis = basis
		return true


	# Records that witness evidence never arrived before a newer basis replaced it.
	func _record_unavailable_operator(
			basis: int,
			transport_waited: bool,
			dissipate_waited: bool,
	) -> void:
		var decisions: Array = _episode[&"decisions"]
		if transport_waited \
				and not bool(_episode.get(&"transport_decided", false)):
			_episode[&"transport_decided"] = true
			decisions.append({
				&"operator": NetwPredictJournal.Operator.TRANSPORT_DELTA,
				&"basis": basis,
				&"eligible": false,
				&"applied": false,
				&"eligibility": {
					&"witness_aligned": false,
				},
			})
		if dissipate_waited \
				and not bool(_episode.get(&"dissipate_decided", false)):
			_episode[&"dissipate_decided"] = true
			decisions.append({
				&"operator": NetwPredictJournal.Operator.DISSIPATE,
				&"basis": basis,
				&"eligible": false,
				&"applied": false,
				&"eligibility": {
					&"witness_aligned": false,
				},
			})
		_sync_episode()


	# Re-evaluates the retained state once its ack witness has been compared.
	func _retry_deferred_operator(basis: int) -> void:
		if _operator_deferred_basis != basis \
				or not _deferred_operator_states.has(basis):
			return
		var state: Dictionary = _deferred_operator_states[basis]
		_deferred_operator_states.erase(basis)
		_operator_deferred_basis = -1
		_on_state(
			int(state[&"recv_tick"]),
			basis,
			state[&"payload"],
		)


	# Attempts the conditional operator once and records every eligibility fact.
	func _try_transport(
			predicted: Dictionary,
			authority: Dictionary,
			current: Dictionary,
			basis: int,
			escalated: bool,
	) -> Dictionary:
		if _handle.transport_config.is_empty() or _episode.is_empty() \
				or bool(_episode.get(&"transport_decided", false)):
			return { }
		var candidate := transport(
			predicted,
			authority,
			current,
			_restore_projection,
			_angle_fields,
		)
		var basis_clean := _witness_row_clean(
			_journal.row_at(basis),
			true,
		)
		var recent_clean := _recent_witness_clean(basis)
		var non_pose := _non_pose_eligibility(predicted, authority)
		var delta_size := _transport_delta_size(
			current,
			candidate.get(&"restore", { }),
		)
		var below_teleport := delta_size > 0.0 \
				and delta_size < _handle.teleport_threshold
		var policy_ok := not escalated \
				and _correction == PredictionHandle.CorrectionMode.SNAP \
				and _handle.resolved_recovery_policy() \
				!= PredictionHandle.RecoveryPolicy.OBSERVE
		var corridor_clear := false
		var prior_ok := bool(candidate.get(&"valid", false)) \
				and basis_clean and recent_clean \
				and bool(non_pose[&"agrees"]) \
				and below_teleport and policy_ok
		if prior_ok:
			var corridor: Callable = _handle.transport_config[&"corridor"]
			var proposed := current.duplicate()
			proposed.merge(candidate[&"restore"], true)
			corridor_clear = bool(corridor.call(current, proposed))
		var eligible := prior_ok and corridor_clear
		var decision := {
			&"operator": NetwPredictJournal.Operator.TRANSPORT_DELTA,
			&"basis": basis,
			&"eligible": eligible,
			&"applied": eligible,
			&"eligibility": {
				&"candidate": bool(candidate.get(&"valid", false)),
				&"basis_witness_clean": basis_clean,
				&"recent_witness_clean": recent_clean,
				&"non_pose": non_pose,
				&"delta": delta_size,
				&"below_teleport": below_teleport,
				&"policy": policy_ok,
				&"corridor_clear": corridor_clear,
			},
		}
		_episode[&"transport_decided"] = true
		var decisions: Array = _episode[&"decisions"]
		decisions.append(decision)
		_sync_episode()
		return candidate if eligible else { }


	# A clean witness is awake and reports only contact the closure reproduces:
	# none, the declared support, or static world geometry.
	func _witness_row_clean(row: Dictionary, require_peer: bool) -> bool:
		if row.is_empty() or not int(row.get(&"evidence_mask", 0)) \
				& NetwPredictJournal.EVIDENCE_WITNESS:
			return false
		if require_peer and not int(row.get(&"flags", 0)) \
				& NetwPredictJournal.ROW_WITNESS_MATCHED:
			return false
		var detail: Dictionary = row.get(&"witness_detail", { })
		if detail.is_empty() or bool(detail.get(&"sleeping", true)) \
				or bool(detail.get(&"woke", true)):
			return false
		var classes: Array = detail.get(&"contact_classes", [])
		if classes.is_empty():
			return false
		for contact_class: int in classes:
			if contact_class not in [
				PredictionHandle.ContactClass.NONE,
				PredictionHandle.ContactClass.DECLARED_SUPPORT,
				PredictionHandle.ContactClass.OTHER_STATIC,
			]:
				return false
		return true


	# Requires a contiguous clean local witness run from the basis through now.
	func _recent_witness_clean(basis: int) -> bool:
		var expected := basis
		for transition: int in _journal.transitions():
			if transition < basis:
				continue
			if transition != expected:
				return false
			var row := _journal.row_at(transition)
			if not int(row.get(&"flags", 0)) \
					& NetwPredictJournal.ROW_CLOSED:
				return expected > basis
			if not _witness_row_clean(row, false):
				return false
			expected += 1
		return expected > basis


	# Compares every declared causal field outside the pose family at the basis.
	func _non_pose_eligibility(
			predicted: Dictionary,
			authority: Dictionary,
	) -> Dictionary:
		var agrees := true
		var fields: Dictionary = { }
		for field: StringName in _causal_fields:
			if int(_state_family_of.get(
				field,
				STATE_FAMILY_CONTROLLER,
			)) == STATE_FAMILY_POSE:
				continue
			var error := INF
			if predicted.has(field) and authority.has(field):
				error = PredictionHandle._error(
					predicted[field],
					authority[field],
					_angle_fields.has(field),
				)
			var epsilon := float(
				_epsilon_overrides.get(field, _handle.divergence_epsilon),
			)
			var field_agrees := error <= epsilon
			fields[field] = {
				&"error": error,
				&"epsilon": epsilon,
				&"agrees": field_agrees,
			}
			agrees = agrees and field_agrees
		return { &"agrees": agrees, &"fields": fields }


	# Measures the largest pose change the partial transport would write.
	func _transport_delta_size(
			current: Dictionary,
			restore: Dictionary,
	) -> float:
		if restore.is_empty():
			return INF
		var error := 0.0
		for field: StringName in restore:
			if not current.has(field):
				return INF
			error = maxf(
				error,
				PredictionHandle._error(
					current[field],
					restore[field],
					_angle_fields.has(field),
				),
			)
		return error


	# Whether transport is still gathering its bounded verification verdicts.
	func _transport_pending() -> bool:
		if _episode.is_empty():
			return false
		var writes: Array = _episode.get(&"writes", [])
		for index in range(writes.size() - 1, -1, -1):
			var write: Dictionary = writes[index]
			if int(write.get(&"operator", -1)) \
					!= NetwPredictJournal.Operator.TRANSPORT_DELTA:
				continue
			return int(write.get(&"outcome", -1)) \
					== PredictionHandle.OperatorOutcome.PENDING
		return false


	# Whether the clean momentum-only no-write attempt is still gathering proof.
	func _dissipate_pending() -> bool:
		if _episode.is_empty():
			return false
		var writes: Array = _episode.get(&"writes", [])
		for index in range(writes.size() - 1, -1, -1):
			var write: Dictionary = writes[index]
			if int(write.get(&"operator", -1)) \
					!= NetwPredictJournal.Operator.DISSIPATE:
				continue
			return int(write.get(&"outcome", -1)) \
					== PredictionHandle.OperatorOutcome.PENDING
		return false


	# Whether this recipe exposes at least one withheld momentum field.
	func _has_dissipate_fields() -> bool:
		return not _withheld_fields.is_empty()


	# Attempts one bounded no-write window for a clean momentum-only error.
	func _try_dissipate(
			basis: int,
			meter: int,
			domain: NetwPredictJournal.Domain,
			escalated: bool,
	) -> bool:
		if _episode.is_empty() \
				or bool(_episode.get(&"dissipate_decided", false)) \
				or _withheld_fields.is_empty():
			return false
		var eligibility := _dissipate_eligibility(basis, domain)
		var eligible := not escalated and meter > 0 \
				and bool(eligibility[&"eligible"])
		_episode[&"dissipate_decided"] = true
		var decisions: Array = _episode[&"decisions"]
		decisions.append({
			&"operator": NetwPredictJournal.Operator.DISSIPATE,
			&"basis": basis,
			&"eligible": eligible,
			&"applied": eligible,
			&"eligibility": eligibility,
		})
		if eligible:
			var write_id := _next_operator_write_id
			_next_operator_write_id += 1
			_record_episode_write(
				write_id,
				NetwPredictJournal.Operator.DISSIPATE,
				basis,
				0,
				_entity.entity_id if _entity else &"body",
				false,
				true,
			)
		else:
			_sync_episode()
		return eligible


	# Proves that only withheld momentum fields cross their declared zero region.
	func _dissipate_eligibility(
			basis: int,
			domain: NetwPredictJournal.Domain,
	) -> Dictionary:
		var tolerances := _meter_tolerances(domain)
		var fields: Dictionary = { }
		var momentum_active := false
		var other_active := false
		for field: StringName in _handle.last_field_divergence:
			if _trigger_excludes.has(field) \
					and not _withheld_fields.has(field):
				continue
			var error := float(_handle.last_field_divergence.get(field, INF))
			var tolerance := float(tolerances.get(
				field,
				_epsilon_overrides.get(
					field,
					_handle.divergence_epsilon,
				),
			))
			var active := error > tolerance
			var momentum := _withheld_fields.has(field)
			fields[field] = {
				&"error": error,
				&"epsilon": tolerance,
				&"active": active,
				&"momentum": momentum,
			}
			if not active:
				continue
			if momentum:
				momentum_active = true
			elif not _trigger_excludes.has(field):
				other_active = true
		var basis_clean := _witness_row_clean(
			_journal.row_at(basis),
			true,
		)
		var recent_clean := _recent_witness_clean(basis)
		var mechanism_ok := _correction \
				== PredictionHandle.CorrectionMode.SNAP \
				and _handle.resolved_recovery_policy() \
				!= PredictionHandle.RecoveryPolicy.OBSERVE
		return {
			&"eligible": momentum_active and not other_active \
					and basis_clean and recent_clean and mechanism_ok,
			&"momentum_active": momentum_active,
			&"other_active": other_active,
			&"basis_witness_clean": basis_clean,
			&"recent_witness_clean": recent_clean,
			&"mechanism": mechanism_ok,
			&"fields": fields,
		}


	# Attaches later mismatches as taint or an agreeing-pre secondary generator.
	func _record_episode_divergence(transition: int) -> void:
		if _episode.is_empty() or int(_episode.get(&"state", -1)) \
				!= PredictionHandle.EpisodeState.OPEN:
			return
		if transition == int(_episode.get(&"opened_transition", -1)):
			return
		var row := _journal.row_at(transition)
		var attribution := int(row.get(
			&"attribution",
			NetwPredictJournal.Attribution.UNKNOWN,
		))
		var independent := not row.is_empty() \
				and attribution != NetwPredictJournal.Attribution.UNKNOWN \
				and attribution != NetwPredictJournal.Attribution.PRE_STATE
		if independent:
			var generators: Array = _episode[&"secondary_generators"]
			for generator: Dictionary in generators:
				if int(generator.get(&"transition", -1)) == transition:
					return
			generators.append(_pin_row(transition))
		else:
			var taint: Array = _episode[&"taint"]
			if not taint.has(transition):
				taint.append(transition)
		_sync_episode()


	# Records one distinct aligned verdict and closes after the verified run.
	func _record_episode_comparison(
			transition: int,
			meter: int,
			agrees: bool,
	) -> void:
		if _episode.is_empty() or int(_episode.get(&"state", -1)) \
				!= PredictionHandle.EpisodeState.OPEN:
			return
		var last := int(_episode.get(&"last_comparison_transition", -1))
		if transition <= last:
			return
		_judge_operator_outcomes(transition, meter)
		var comparisons: Array = _episode[&"comparisons"]
		comparisons.append({
			&"transition": transition,
			&"meter": meter,
			&"agrees": agrees,
			&"write_id": int(_episode.get(&"last_write_id", 0)),
		})
		_episode[&"last_comparison_transition"] = transition
		if agrees:
			var same_write := int(_episode.get(&"agreement_write_id", 0)) \
					== int(_episode.get(&"last_write_id", 0))
			_episode[&"agreement_run"] = (
				int(_episode.get(&"agreement_run", 0)) + 1
				if same_write else 1
			)
			_episode[&"agreement_write_id"] = int(
				_episode.get(&"last_write_id", 0),
			)
		else:
			_episode[&"agreement_run"] = 0
		if int(_episode[&"agreement_run"]) >= EPISODE_CLOSE_RUN:
			_close_episode(transition)
		else:
			_sync_episode()


	# Judges pending operator writes from later aligned meter samples.
	func _judge_operator_outcomes(transition: int, meter: int) -> void:
		var writes: Array = _episode.get(&"writes", [])
		for i in writes.size():
			var write: Dictionary = writes[i]
			if int(write.get(&"outcome", -1)) \
					!= PredictionHandle.OperatorOutcome.PENDING:
				continue
			if transition <= int(write.get(&"basis", -1)):
				continue
			if int(write.get(&"operator", -1)) \
					== NetwPredictJournal.Operator.DISSIPATE:
				var best := int(write.get(
					&"best_meter",
					write.get(&"meter_before", meter),
				))
				if meter == 0:
					write[&"outcome"] = \
							PredictionHandle.OperatorOutcome.CONTRACTED
					write[&"judged_transition"] = transition
					write[&"meter_after"] = meter
				elif meter < best:
					write[&"best_meter"] = meter
					write[&"verdicts"] = 0
				else:
					write[&"verdicts"] = int(
						write.get(&"verdicts", 0),
					) + 1
					if int(write[&"verdicts"]) >= int(
						write.get(&"verify_run", 3),
					):
						write[&"outcome"] = (
							PredictionHandle.OperatorOutcome.FAILED_TO_CONTRACT
						)
						write[&"judged_transition"] = transition
						write[&"meter_after"] = meter
						_episode[&"non_contraction_used"] = int(
							_episode.get(&"non_contraction_used", 0),
						) + 1
				writes[i] = write
				continue
			if meter == 0 or meter < int(write.get(&"meter_before", meter)):
				write[&"outcome"] = PredictionHandle.OperatorOutcome.CONTRACTED
				write[&"judged_transition"] = transition
				write[&"meter_after"] = meter
				writes[i] = write
				continue
			write[&"verdicts"] = int(write.get(&"verdicts", 0)) + 1
			if int(write[&"verdicts"]) >= int(write.get(&"verify_run", 3)):
				write[&"outcome"] = \
						PredictionHandle.OperatorOutcome.FAILED_TO_CONTRACT
				write[&"judged_transition"] = transition
				write[&"meter_after"] = meter
				if not bool(write.get(&"evidence_free", false)):
					_episode[&"non_contraction_used"] = int(
						_episode.get(&"non_contraction_used", 0),
					) + 1
			writes[i] = write


	# Records one operator attempt as episode evidence and resets the clean run.
	func _record_episode_write(
			write_id: int,
			operator: NetwPredictJournal.Operator,
			basis: int,
			delta_fp: int,
			target: StringName,
			evidence_free: bool = false,
			null_operator: bool = false,
	) -> void:
		if _episode.is_empty() or int(_episode.get(&"state", -1)) not in [
			PredictionHandle.EpisodeState.OPEN,
			PredictionHandle.EpisodeState.FALLBACK,
		]:
			return
		var comparisons: Array = _episode.get(&"comparisons", [])
		var meter_before := 0x7FFFFFFF
		if not comparisons.is_empty():
			meter_before = int(comparisons.back().get(&"meter", meter_before))
		var writes: Array = _episode[&"writes"]
		writes.append({
			&"episode": int(_episode[&"id"]),
			&"write_id": write_id,
			&"operator": operator,
			&"basis": basis,
			&"delta_fp": delta_fp,
			&"target": target,
			&"outcome": PredictionHandle.OperatorOutcome.PENDING,
			&"meter_before": meter_before,
			&"verdicts": 0,
			&"verify_run": EPISODE_CLOSE_RUN if null_operator \
					else maxi(3, _handle.ack_age_ticks),
			&"evidence_free": evidence_free,
			&"null_operator": null_operator,
		})
		_episode[&"last_write_id"] = write_id
		_episode[&"agreement_run"] = 0
		if operator == NetwPredictJournal.Operator.FULL_CLOSURE:
			_episode[&"closure_used"] = int(
				_episode.get(&"closure_used", 0),
			) + 1
		_sync_episode()


	# Spends escalation evidence without resetting any episode counter.
	func _record_non_contraction() -> void:
		if _episode.is_empty() or int(_episode.get(&"state", -1)) \
				!= PredictionHandle.EpisodeState.OPEN:
			return
		var writes: Array = _episode.get(&"writes", [])
		for i in range(writes.size() - 1, -1, -1):
			var write: Dictionary = writes[i]
			if int(write.get(&"outcome", -1)) \
					!= PredictionHandle.OperatorOutcome.PENDING:
				continue
			write[&"outcome"] = \
					PredictionHandle.OperatorOutcome.FAILED_TO_CONTRACT
			writes[i] = write
			break
		_episode[&"non_contraction_used"] = int(
			_episode.get(&"non_contraction_used", 0),
		) + 1
		_sync_episode()


	# Retires the episode after the domain-aware verified agreement run.
	func _close_episode(transition: int) -> void:
		_episode[&"state"] = PredictionHandle.EpisodeState.CLOSED
		_episode[&"closed_transition"] = transition
		_handle._store_episode_closure(_episode, transition)
		_handle.episode_closed.emit(_handle._episode_report(_episode))


	# Whether either structural recovery evidence budget is exhausted.
	func _episode_budget_exhausted() -> bool:
		if _episode.is_empty() or int(_episode.get(&"state", -1)) \
				!= PredictionHandle.EpisodeState.OPEN:
			return false
		return int(_episode.get(&"non_contraction_used", 0)) \
				>= EPISODE_NON_CONTRACTION_BUDGET \
				or int(_episode.get(&"closure_used", 0)) \
				>= EPISODE_FULL_CLOSURE_BUDGET


	# Closes speculation and starts the coherent-authority quarantine proof.
	func _enter_fallback(transition: int, demoted: bool = false) -> void:
		var stream_was_reconstructed := _stream_reconstructed
		_episode[&"state"] = PredictionHandle.EpisodeState.FALLBACK
		_episode[&"fallback_transition"] = transition
		_episode[&"demoted"] = demoted
		_fallback_latched = true
		var resume_ack_age := maxi(_handle.ack_age_ticks, 0)
		var resume_run := _resume_run_for_ack_age(resume_ack_age)
		_quarantine_target = _handle._quarantine_target(
			resume_run,
			QUARANTINE_RUN_CAP,
		)
		_episode[&"resume_ack_age"] = resume_ack_age
		_episode[&"quarantine_target"] = _quarantine_target
		_sync_episode()
		_handle.episode_fallback.emit(_handle._episode_report(_episode))
		_rewire()
		_quarantine_stream_reconstructed = stream_was_reconstructed
		_quarantine_clean_run = 0
		_quarantine_last_tick = -1
		_quarantine_last_basis = -1
		_quarantine_pending_states = { }


	# Scales the clean proof by measured acknowledgement age, never wire width.
	func _resume_run_for_ack_age(ack_age: int) -> int:
		return clampi(
			maxi(ack_age, 0) * RESUME_ACK_MULTIPLIER,
			RESUME_RUN_MIN,
			QUARANTINE_RUN_CAP,
		)


	# Counts distinct coherent authority frames while speculation is closed.
	func _on_quarantine_state_frame(header: Dictionary) -> void:
		if not _fallback_latched:
			return
		_quarantine_stream_reconstructed = \
				_quarantine_stream_reconstructed \
				or bool(header.get(&"whole", true))
		if not _quarantine_stream_reconstructed:
			return
		var tick := int(header.get(&"tick", -1))
		var basis := int(header.get(&"ack", -1))
		var payload: Dictionary = header.get(&"payload", { })
		if tick <= _quarantine_last_tick or basis < 0 or payload.is_empty():
			return
		_quarantine_last_tick = tick
		_quarantine_pending_states[basis] = header.duplicate(true)
		while _quarantine_pending_states.size() > TAPE_HISTORY_LIMIT:
			var retained := _quarantine_pending_states.keys()
			retained.sort()
			_quarantine_pending_states.erase(retained.front())
		if _authority_witness_classes.has(basis):
			_apply_quarantine_witness(basis)
		_try_begin_quarantine_reseed()


	# Counts one transition only when its authority witness summary arrives.
	func _apply_quarantine_witness(basis: int) -> void:
		if not _fallback_latched or basis <= _quarantine_last_basis:
			return
		var witness_bits := int(_authority_witness_classes.get(basis, -1))
		var clean := _authority_witness_is_clean(witness_bits)
		if not clean:
			_quarantine_clean_run = 0
		elif _quarantine_last_basis >= 0 \
				and basis != _quarantine_last_basis + 1:
			_quarantine_clean_run = 1
		else:
			_quarantine_clean_run += 1
		_quarantine_last_basis = basis
		_episode[&"quarantine_clean_run"] = _quarantine_clean_run
		_sync_episode()
		_try_begin_quarantine_reseed()


	# Reseeds from the newest coherent state inside the proven clean run.
	func _try_begin_quarantine_reseed() -> void:
		if not _fallback_latched \
				or _quarantine_clean_run < _quarantine_target:
			return
		var clean_start := _quarantine_last_basis \
				- _quarantine_clean_run + 1
		var bases := _quarantine_pending_states.keys()
		bases.sort()
		bases.reverse()
		for value: Variant in bases:
			var basis := int(value)
			if basis < clean_start or basis > _quarantine_last_basis:
				continue
			if not _authority_witness_is_clean(int(
					_authority_witness_classes.get(basis, -1),
			)):
				continue
			_begin_reseed(_quarantine_pending_states[basis])
			return


	# An authority summary is clean when it reports only contact the closure
	# reproduces: none, the declared support, or static world geometry.
	func _authority_witness_is_clean(bits: int) -> bool:
		return bits >= 0 and not bool(
			bits & ~(
				PredictionHandle.WitnessClass.SUPPORT
				| PredictionHandle.WitnessClass.STATIC
			),
		)


	# Seeds the newest authority closure and reopens a fresh command epoch.
	func _begin_reseed(header: Dictionary) -> void:
		var payload: Dictionary = header.get(&"payload", { })
		var basis := int(header.get(&"ack", -1))
		_restore(
			payload,
			NetwPredictJournal.Operator.RESEED,
			basis,
			null,
			true,
		)
		var reseed_provenance := _pending_provenance.duplicate(true)
		_episode[&"reseed_transition"] = basis
		_fallback_latched = false
		_reseed_align_pending = true
		_reseed_epoch_confirmed = false
		_reseed_ignore_through = -1
		_rewire()
		_pending_provenance = reseed_provenance
		_sync_episode()


	# Applies the one evidence-free post-epoch horizon alignment.
	func _finish_reseed_alignment(
			recv_tick: int,
			ack: int,
			payload: Dictionary,
	) -> void:
		_restore(
			payload,
			NetwPredictJournal.Operator.RESEED_ALIGN,
			ack,
			null,
			true,
		)
		if _timeline:
			_timeline.record_state(ack + 1, payload)
		if _correction == PredictionHandle.CorrectionMode.REPLAY:
			_replay_reseed_horizon(ack)
		_reseed_ignore_through = (
			_last_driven_entry_index
			if _handle._schedule == PredictionHandle.Schedule.FRAME
			else _latest_input_tick
		)
		_reseed_align_pending = false
		_reseed_epoch_confirmed = false
		_episode[&"aligned_transition"] = ack
		_episode[&"closed_transition"] = ack
		_handle._store_episode_closure(_episode, ack)
		_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)


	# Re-runs the current epoch's unacknowledged commands from the aligned seed.
	func _replay_reseed_horizon(ack: int) -> void:
		if _handle._schedule == PredictionHandle.Schedule.FRAME:
			_replay_authored_entries(ack)
			return
		if not _timeline or not _input_binding:
			return
		var window := _timeline.inputs_in_range(ack + 1, _latest_input_tick)
		var live_input := _input_binding.snapshot_payload()
		for entry: Dictionary in window:
			_run(entry[&"input"], _tick_delta, int(entry[&"tick"]), false)
			_timeline.record_state(int(entry[&"tick"]) + 1, _capture())
		_input_binding.apply_payload(live_input)
		_handle.max_replay_depth = maxi(
			_handle.max_replay_depth,
			window.size(),
		)


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
		var authors_commands := _role == PredictionHandle.Role.PREDICT \
				or _role == PredictionHandle.Role.REMOTE and _fallback_latched
		if not authors_commands or _authored_tape.is_empty():
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
		var pre_fps := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var pre_family_fps := PackedInt32Array()
		var post_family_fps := PackedInt32Array()
		var topo_fps := PackedInt32Array()
		var witness_fps := PackedInt32Array()
		var raw_fps := PackedInt32Array()
		var evidence_masks := PackedByteArray()
		var closed := _journal.last_closed()
		for entry: Dictionary in rows:
			var index := int(entry.get("index", -1))
			if index > closed:
				break
			var row := _journal.row_at(index)
			if row.is_empty():
				break
			pre_fps.append(int(row.get(&"pre_fp", 0)))
			post_fps.append(int(row.get(&"post_fp", 0)))
			e_digests.append(int(row.get(&"e_digest", 0)))
			topo_fps.append(int(row.get(&"topo_fp", 0)))
			witness_fps.append(int(row.get(&"witness_fp", 0)))
			raw_fps.append(int(row.get(&"raw_fp", 0)))
			evidence_masks.append(int(row.get(&"evidence_mask", 0)))
			for key in [&"pre_pose_fp", &"pre_momentum_fp", &"pre_controller_fp"]:
				pre_family_fps.append(int(row.get(key, 0)))
			for key in [&"post_pose_fp", &"post_momentum_fp", &"post_controller_fp"]:
				post_family_fps.append(int(row.get(key, 0)))
		return _PredictFrames.encode_command(
			_tape_epoch,
			_ack_of_acks,
			rows,
			payloads,
			quantizers,
			types,
			post_fps,
			e_digests,
			pre_fps,
			pre_family_fps,
			post_family_fps,
			topo_fps,
			witness_fps,
			raw_fps,
			evidence_masks,
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
		var all_pre := _journal.pre_fps()
		var all_c := _journal.c_hashes()
		var all_e := _journal.e_digests()
		var all_fp := _journal.post_fps()
		var all_pre_families := _journal.pre_family_fps()
		var all_post_families := _journal.post_family_fps()
		var all_topo := _journal.topo_fps()
		var all_witness := _journal.witness_fps()
		var all_witness_classes := _journal.witness_class_bits()
		var all_raw := _journal.raw_fps()
		var all_evidence := _journal.evidence_masks()
		var all_flags := _journal.flags()
		var pre_fps := PackedInt32Array()
		var c_hashes := PackedInt32Array()
		var e_digests := PackedInt32Array()
		var post_fps := PackedInt32Array()
		var pre_family_fps := PackedInt32Array()
		var post_family_fps := PackedInt32Array()
		var topo_fps := PackedInt32Array()
		var witness_fps := PackedInt32Array()
		var raw_fps := PackedInt32Array()
		var evidence_masks := PackedByteArray()
		var flags := PackedByteArray()
		for i in transitions.size():
			var transition := transitions[i]
			if transition < base:
				continue
			if transition > frontier:
				break
			pre_fps.append(all_pre[i])
			c_hashes.append(all_c[i])
			e_digests.append(all_e[i])
			post_fps.append(all_fp[i])
			topo_fps.append(all_topo[i])
			witness_fps.append(all_witness[i])
			raw_fps.append(all_raw[i])
			evidence_masks.append(all_evidence[i])
			for family in 3:
				pre_family_fps.append(all_pre_families[i * 3 + family])
				post_family_fps.append(all_post_families[i * 3 + family])
			var ack_flags := _PredictFrames.ACK_SUBSTITUTED \
					if all_flags[i] & NetwPredictJournal.ROW_SUBSTITUTED else 0
			ack_flags |= (
				all_witness_classes[i] << _PredictFrames.ACK_WITNESS_SHIFT
			) & _PredictFrames.ACK_WITNESS_MASK
			flags.append(ack_flags)
		if flags.is_empty():
			return PackedByteArray()
		return _PredictFrames.encode_ack(
			_command_epoch,
			base,
			pre_fps,
			c_hashes,
			e_digests,
			post_fps,
			pre_family_fps,
			post_family_fps,
			topo_fps,
			witness_fps,
			raw_fps,
			evidence_masks,
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
		var pre_fps: PackedInt32Array = frame.get("pre_fps", PackedInt32Array())
		var e_digests: PackedInt32Array = frame.get(
			"e_digests",
			PackedInt32Array(),
		)
		var pre_family_fps: PackedInt32Array = frame.get(
			"pre_family_fps",
			PackedInt32Array(),
		)
		var post_family_fps: PackedInt32Array = frame.get(
			"post_family_fps",
			PackedInt32Array(),
		)
		var topo_fps: PackedInt32Array = frame.get(
			"topo_fps",
			PackedInt32Array(),
		)
		var witness_fps: PackedInt32Array = frame.get(
			"witness_fps",
			PackedInt32Array(),
		)
		var raw_fps: PackedInt32Array = frame.get(
			"raw_fps",
			PackedInt32Array(),
		)
		var evidence_masks: PackedByteArray = frame.get(
			"evidence_masks",
			PackedByteArray(),
		)
		for i in post_fps.size():
			if i >= transitions.size():
				break
			var claimed := int((transitions[i] as Dictionary).get("index", -1))
			if claimed < 0:
				continue
			_owner_claims[claimed] = {
				&"pre_fp": pre_fps[i],
				&"post_fp": post_fps[i],
				&"e_digest": e_digests[i],
				&"topo_fp": topo_fps[i],
				&"witness_fp": witness_fps[i],
				&"raw_fp": raw_fps[i],
				&"evidence_mask": evidence_masks[i],
				&"pre_pose_fp": pre_family_fps[i * 3],
				&"pre_momentum_fp": pre_family_fps[i * 3 + 1],
				&"pre_controller_fp": pre_family_fps[i * 3 + 2],
				&"post_pose_fp": post_family_fps[i * 3],
				&"post_momentum_fp": post_family_fps[i * 3 + 1],
				&"post_controller_fp": post_family_fps[i * 3 + 2],
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
			if _handle._schedule == PredictionHandle.Schedule.TICK \
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
		if _role != PredictionHandle.Role.PREDICT and not _fallback_latched:
			return
		var frame := _PredictFrames.decode_ack(payload)
		if frame.is_empty():
			_handle.frames_dropped_invalid += 1
			return
		if int(frame.get(&"epoch", -1)) != _tape_epoch:
			return
		if _reseed_align_pending:
			_reseed_epoch_confirmed = true
		_on_ack_run(
			int(frame.get("base", 0)),
			frame.get("pre_fps", PackedInt32Array()),
			frame.get("c_hashes", PackedInt32Array()),
			frame.get("e_digests", PackedInt32Array()),
			frame.get("post_fps", PackedInt32Array()),
			frame.get("pre_family_fps", PackedInt32Array()),
			frame.get("post_family_fps", PackedInt32Array()),
			frame.get("topo_fps", PackedInt32Array()),
			frame.get("witness_fps", PackedInt32Array()),
			frame.get("raw_fps", PackedInt32Array()),
			frame.get("evidence_masks", PackedByteArray()),
			frame.get("flags", PackedByteArray()),
			frame.get("witness_class_bits", PackedByteArray()),
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
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			post_fps: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
			flags: PackedByteArray,
			witness_class_bits: PackedByteArray = PackedByteArray(),
	) -> void:
		for i in flags.size():
			var transition := base + i
			_ack_of_acks = maxi(_ack_of_acks, transition)
			_latest_authority_ack = maxi(_latest_authority_ack, transition)
			if i < witness_class_bits.size():
				_authority_witness_classes[transition] = witness_class_bits[i]
				if _fallback_latched:
					_apply_quarantine_witness(transition)
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
						_charge_divergence(
							transition,
							row,
							pre_fps,
							c_hashes,
							e_digests,
							pre_family_fps,
							post_family_fps,
							topo_fps,
							witness_fps,
							raw_fps,
							evidence_masks,
							i,
						)
		_handle.ack_confirmed_transition = _ack_of_acks
		while _authority_witness_classes.size() > TAPE_HISTORY_LIMIT:
			var retained := _authority_witness_classes.keys()
			retained.sort()
			_authority_witness_classes.erase(retained[0])
		_refresh_owner_ack_age()


	# Names the antecedent one disagreeing transition is charged to. The columns
	# are indexed rather than addressed because the caller is already walking
	# them, and a run acknowledging the whole window would otherwise rescan the
	# ring once per record.
	func _charge_divergence(
			transition: int,
			row: Dictionary,
			pre_fps: PackedInt32Array,
			c_hashes: PackedInt32Array,
			e_digests: PackedInt32Array,
			pre_family_fps: PackedInt32Array,
			post_family_fps: PackedInt32Array,
			topo_fps: PackedInt32Array,
			witness_fps: PackedInt32Array,
			raw_fps: PackedInt32Array,
			evidence_masks: PackedByteArray,
			index: int,
	) -> void:
		var family_end := (index + 1) * 3
		var complete := index < pre_fps.size() \
				and index < c_hashes.size() \
				and index < e_digests.size() \
				and index < topo_fps.size() \
				and index < witness_fps.size() \
				and index < raw_fps.size() \
				and index < evidence_masks.size() \
				and family_end <= pre_family_fps.size() \
				and family_end <= post_family_fps.size()
		var local_evidence := int(row.get(&"evidence_mask", 0))
		var peer_evidence := evidence_masks[index] if complete else 0
		var both_witness := complete \
				and bool(local_evidence & NetwPredictJournal.EVIDENCE_WITNESS) \
				and bool(peer_evidence & NetwPredictJournal.EVIDENCE_WITNESS)
		var witness_equal := both_witness \
				and int(row.get(&"witness_fp", 0)) == witness_fps[index]
		_witness_verdicts[transition] = 1 if witness_equal else 2
		while _witness_verdicts.size() > TAPE_HISTORY_LIMIT:
			var verdict_transitions := _witness_verdicts.keys()
			verdict_transitions.sort()
			_witness_verdicts.erase(verdict_transitions[0])
		_journal.mark_witness_match(
			transition,
			witness_equal,
		)
		var attribution := attribute(
			complete and int(row.get(&"pre_fp", 0)) == pre_fps[index],
			complete and int(row.get(&"c_hash", 0)) == c_hashes[index],
			complete and int(row.get(&"e_digest", 0)) == e_digests[index],
			complete and int(row.get(&"topo_fp", 0)) == topo_fps[index],
			complete and int(row.get(&"raw_fp", 0)) == raw_fps[index],
			complete and int(row.get(&"witness_fp", 0)) == witness_fps[index],
			local_evidence,
			peer_evidence,
			complete,
		)
		_handle.last_attribution = attribution
		_handle.last_attributed_transition = transition
		# The charge also lands on the row itself, so a comparison answering
		# for an older transition later still reads the charge that transition
		# earned rather than whatever this lane judged last.
		_journal.mark_attribution(transition, attribution)
		if not complete:
			_retry_deferred_operator(transition)
			return
		var local_family := PackedInt32Array()
		var peer_family := PackedInt32Array()
		if attribution == NetwPredictJournal.Attribution.PRE_STATE:
			local_family = PackedInt32Array([
				int(row.get(&"pre_pose_fp", 0)),
				int(row.get(&"pre_momentum_fp", 0)),
				int(row.get(&"pre_controller_fp", 0)),
			])
			peer_family = pre_family_fps.slice(index * 3, family_end)
		elif attribution == NetwPredictJournal.Attribution.CLOSURE:
			local_family = PackedInt32Array([
				int(row.get(&"post_pose_fp", 0)),
				int(row.get(&"post_momentum_fp", 0)),
				int(row.get(&"post_controller_fp", 0)),
			])
			peer_family = post_family_fps.slice(index * 3, family_end)
		_journal.mark_differing_family(
			transition,
			differing_family(local_family, peer_family),
		)
		_retry_deferred_operator(transition)


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
		_close_journal_row(transition, payload)
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
		var row := _journal.row_at(transition)
		var both_witness := bool(
			int(row.get(&"evidence_mask", 0))
			& NetwPredictJournal.EVIDENCE_WITNESS,
		) and bool(
			int(claim[&"evidence_mask"])
			& NetwPredictJournal.EVIDENCE_WITNESS,
		)
		_journal.mark_witness_match(
			transition,
			both_witness and int(claim[&"witness_fp"]) \
					== int(row.get(&"witness_fp", 0)),
		)
		var attribution := attribute(
			int(claim[&"pre_fp"]) == int(row.get(&"pre_fp", 0)),
			not substituted,
			int(claim[&"e_digest"]) == int(
				row.get(&"e_digest", 0),
			),
			int(claim[&"topo_fp"]) == int(row.get(&"topo_fp", 0)),
			int(claim[&"raw_fp"]) == int(row.get(&"raw_fp", 0)),
			int(claim[&"witness_fp"]) == int(row.get(&"witness_fp", 0)),
			int(row.get(&"evidence_mask", 0)),
			int(claim[&"evidence_mask"]),
		)
		_journal.mark_attribution(transition, attribution)
		var local_family := PackedInt32Array()
		var peer_family := PackedInt32Array()
		if attribution == NetwPredictJournal.Attribution.PRE_STATE:
			local_family = PackedInt32Array([
				int(row.get(&"pre_pose_fp", 0)),
				int(row.get(&"pre_momentum_fp", 0)),
				int(row.get(&"pre_controller_fp", 0)),
			])
			peer_family = PackedInt32Array([
				int(claim[&"pre_pose_fp"]),
				int(claim[&"pre_momentum_fp"]),
				int(claim[&"pre_controller_fp"]),
			])
		elif attribution == NetwPredictJournal.Attribution.CLOSURE:
			local_family = PackedInt32Array([
				int(row.get(&"post_pose_fp", 0)),
				int(row.get(&"post_momentum_fp", 0)),
				int(row.get(&"post_controller_fp", 0)),
			])
			peer_family = PackedInt32Array([
				int(claim[&"post_pose_fp"]),
				int(claim[&"post_momentum_fp"]),
				int(claim[&"post_controller_fp"]),
			])
		_journal.mark_differing_family(
			transition,
			differing_family(local_family, peer_family),
		)
		var iface := _iface()
		if iface and _entity:
			iface.peer_divergence.emit(_entity.controller, transition, attribution)


	# The transition the recorder's slot belongs to. FRAME replays acknowledge the
	# entry index they replayed, every other tier drives its own label.
	func _recorded_transition() -> int:
		if _handle._schedule == PredictionHandle.Schedule.FRAME \
				and _role == PredictionHandle.Role.CONSUME:
			return _ack
		return _handle.last_drive_label


	func order_key() -> String:
		return str(_entity.entity_id) if _entity else ""


	func history_record_tick(fallback_tick: int) -> int:
		if _role == PredictionHandle.Role.CONSUME \
				and _handle._schedule == PredictionHandle.Schedule.FRAME:
			return _last_replayed_label + 1 \
			if _ack_advanced and _last_replayed_fresh else -1
		if _role == PredictionHandle.Role.CONSUME and _ack >= 0:
			# A tick that consumed nothing produced no new authoritative state, so
			# the slot keeps the snapshot the last real consume left there instead
			# of being overwritten with a body that has coasted past the ack.
			return _ack + 1 if _ack_advanced else -1
		if _handle._schedule == PredictionHandle.Schedule.FRAME:
			return _last_driven_input_tick + 1 if _ack_advanced else -1
		return fallback_tick


	# True when the last pass replayed a transition that declines a history
	# slot. A REPEAT entry re-runs its label, so the label's slot keeps the
	# fresh entry's state, but the journal row the replay opened still owes its
	# close, or the acknowledgement frontier stalls behind it forever.
	func consumed_unslotted_transition() -> bool:
		return _role == PredictionHandle.Role.CONSUME \
				and _handle._schedule == PredictionHandle.Schedule.FRAME \
				and _ack_advanced and not _last_replayed_fresh


	func has_consumed_state_tick(state_tick: int) -> bool:
		if _role != PredictionHandle.Role.CONSUME:
			return true
		if _handle._schedule == PredictionHandle.Schedule.FRAME:
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
		_apply_scene_island_defaults()
		_rewire()


	# Inherits the containing scene's rule unless this entity declared its own.
	func _apply_scene_island_defaults() -> void:
		if bool(_handle.island_config.get(&"declared", false)) \
				and not bool(_handle.island_config.get(&"inherited", false)):
			return
		var scene := _entity.scene
		var inherited := scene._prediction_island_defaults() if scene else { }
		if inherited.is_empty() \
				and not bool(_handle.island_config.get(&"inherited", false)):
			return
		for key in [&"sensors", &"epoch"]:
			if _handle.island_config.has(key):
				inherited[key] = _handle.island_config[key]
		_handle.island_config = inherited


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
		_state_family_of = { }
		_causal_fields = { }
		_angle_fields = { }
		_converge_rules = { }
		_cooldown_until_tick = -1
		# A rewire re-keys the transitions the evidence was gathered over, so a
		# streak cannot mean anything across it.
		_reset_recovery_trackers()
		if not _episode.is_empty() and int(_episode.get(&"state", -1)) \
				== PredictionHandle.EpisodeState.OPEN:
			_episode[&"agreement_run"] = 0
			_episode[&"last_comparison_transition"] = -1
			_sync_episode()
		_pending_provenance = { }
		_next_operator_write_id = int(_episode.get(&"last_write_id", 0)) + 1
		_forensic_sidecars = { }
		_previous_witness_sleeping = false
		_has_previous_witness = false
		_invalid_witness_reported = false
		_invalid_command_predictor_reported = false
		_witness_verdicts = { }
		_authority_witness_classes = { }
		_quarantine_pending_states = { }
		_quarantine_last_basis = -1
		_deferred_operator_states = { }
		_operator_deferred_basis = -1
		_raw_fp_enabled = not OS.get_environment(RAW_FP_ENV).is_empty()
		_latest_input_tick = -1
		_last_driven_input_tick = -1
		_last_frame_transition_tick = -1
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
		_handle.chain_break_count = 0
		# A rewire re-keys the transitions a window was expressed in, so an open
		# window cannot mean anything across it and is dropped rather than carried.
		_out_of_domain_until = -1
		_e_digest = 0
		_island_epoch = -1
		_sensor_samples = { }
		_command_queue.clear()
		_command_epoch = -1
		_ack_of_acks = -1
		_latest_authority_ack = -1
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
		if _role != PredictionHandle.Role.PREDICT:
			_clear_island_promotions()
		_correction = PredictionHandle.resolve_correction_mode_for(
			_entity.owner,
			_handle.correction_mode,
		)
		_error_on_retained_predicted_props(state_binding.set)
		_build_restore_projection(state_binding)
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
			PredictionHandle.Role.SIMULATE:
				# Authority rows are decoded without snapping, then projected to the
				# current transition before each independent rebase.
				_timeline = NetwTimeline.new()
				_entity.timeline = _timeline
				input_binding.volatile_external = true
				state_binding.write_gate = false
				state_binding.on_applied = _on_simulated_state_frame
				_register_with_loop()
			PredictionHandle.Role.REMOTE:
				if _fallback_latched:
					# Fallback still owns the command lane. It receives authority
					# state for display but runs no speculative simulation.
					_timeline = NetwTimeline.new()
					_entity.timeline = _timeline
					input_binding.volatile_external = true
					state_binding.on_applied = _on_quarantine_state_frame
					_register_with_loop()
		_entity.interpolation._mark_role_dirty()


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
		_state_family_of = { }
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
			_state_family_of[field.key] = STATE_FAMILY_CONTROLLER
			if field.property_class == NetwSyncSet.PropertyClass.CAUSAL:
				_causal_fields[field.key] = true
		for field in binding.set.fields:
			var spec := NetwScriptModel.get_node_property_interpolator(node, field.key)
			if not spec:
				continue
			if spec.mode == NetwInterpolate.Mode.ANGLE:
				_angle_fields[field.key] = true
			if spec.project_channel == &"":
				continue
			if not field_keys.has(spec.project_channel):
				continue
			_state_family_of[field.key] = STATE_FAMILY_POSE
			_state_family_of[spec.project_channel] = STATE_FAMILY_MOMENTUM
			if spec.forecast_tail == NetwInterpolate.Tail.HOLD:
				continue
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
		var transport_without_epsilon: Array[String] = []
		for field: NetwSyncSet.Field in set.fields:
			match field.property_class:
				NetwSyncSet.PropertyClass.CAUSAL:
					if not field.quantizer:
						unquantized.append(String(field.key))
				NetwSyncSet.PropertyClass.COSMETIC:
					if field.epsilon_override >= 0.0:
						uncompared.append(String(field.key))
			if not _handle.transport_config.is_empty() \
					and field.property_class == NetwSyncSet.PropertyClass.CAUSAL \
					and int(_state_family_of.get(
						field.key,
						STATE_FAMILY_CONTROLLER,
					)) != STATE_FAMILY_POSE \
					and field.epsilon_override < 0.0:
				transport_without_epsilon.append(String(field.key))
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
		if not transport_without_epsilon.is_empty():
			Netw.dbg.warn(
				(
						"NetwLagCompensation: transport reads causal non-pose "
						+ "properties [%s] without declared epsilons. Their units "
						+ "fall back to the entity default, so the transport gate "
						+ "has no world-scale tolerance."
				),
				[", ".join(transport_without_epsilon)],
				func(m): push_warning(m),
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
		var node := _entity.owner if _entity else null
		var script := node.get_script() as Script if is_instance_valid(node) \
				else null
		if script:
			var broadcast_set := NetwSyncSet.from_script(
				script,
				NetwSyncSet.Record.RECORD_BROADCAST,
			)
			if broadcast_set:
				for field: NetwSyncSet.Field in broadcast_set.fields:
					parts.append("broadcast:%s" % field.key)
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
		elif not _handle._simulation_subjects.is_empty():
			_handle.input_source = PredictionHandle.InputSource.PREDICTED
			_handle.sim_mode = PredictionHandle.SimMode.SPECULATIVE
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
		return _fallback_latched or _handle.recovery_policy \
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


	# Produces the interest-bounded roster and commits hysteretic promotion.
	func _refresh_island_membership(apply_promotion: bool = true) -> void:
		if not bool(_handle.island_config.get(&"declared", false)):
			_clear_island_promotions()
			_island_members.clear()
			return
		var found: Dictionary[NetwEntity, bool] = { }
		for entry: Variant in _handle.island_config.get(&"participants", []):
			var explicit := entry as NetwEntity
			if _valid_island_member(explicit):
				found[explicit] = true
		var api := _entity.multiplayer
		if api:
			for producer: Dictionary in _handle.island_config.get(
					&"producers",
					[],
			):
				if producer.get(&"kind", &"") != &"interest":
					continue
				var layer: StringName = producer.get(&"layer", &"")
				for candidate: NetwEntity in api.interest.shared_entities(
						_entity,
						layer,
				):
					if _valid_island_member(candidate):
						found[candidate] = true
		var members: Array[NetwEntity] = []
		members.assign(found.keys())
		members.sort_custom(_entity_id_less)
		if not apply_promotion:
			_clear_island_promotions()
			_island_members = members
			return
		var desired := _desired_simulated_members(members)
		var committed: Dictionary[NetwEntity, bool] = { }
		for member: NetwEntity in members:
			var was_simulated := _simulated_members.has(member)
			var wants_simulated := desired.has(member)
			if was_simulated != wants_simulated \
					and _realized_contact_entities.has(member):
				wants_simulated = was_simulated
			if wants_simulated:
				committed[member] = true
		_enforce_nearest_budget(committed)
		_apply_island_promotions(committed)
		_island_members = members


	# Returns whether a locally present participant may enter this island.
	# A scene wrapper is a container, never an interaction participant, so no
	# producer or explicit add may seat one in the island.
	func _valid_island_member(member: NetwEntity) -> bool:
		return member != null and member != _entity \
				and is_instance_valid(member) \
				and is_instance_valid(member.owner) \
				and not member.owner is MultiplayerScene \
				and member.multiplayer == _entity.multiplayer \
				and member.scene == _entity.scene


	# Selects explicit and policy-driven participants, retaining near-boundary
	# winners so local distance noise cannot flip body topology every transition.
	func _desired_simulated_members(
			members: Array[NetwEntity],
	) -> Dictionary[NetwEntity, bool]:
		var desired: Dictionary[NetwEntity, bool] = { }
		var fidelity: Dictionary = _handle.island_config.get(&"fidelity", { })
		var automatic: Array[NetwEntity] = []
		for member: NetwEntity in members:
			var explicit := int(fidelity.get(member, -1))
			if explicit == PredictionHandle.Fidelity.SIMULATED:
				desired[member] = true
			elif explicit != PredictionHandle.Fidelity.PROXY \
					and _can_automatically_simulate(member):
				automatic.append(member)
		var promotion: Dictionary = _handle.island_config.get(&"promotion", { })
		match promotion.get(&"kind", &""):
			&"nearest":
				_promote_nearest(
					automatic,
					int(promotion.get(&"count", 0)),
					desired,
				)
			&"within":
				_promote_within(
					automatic,
					float(promotion.get(&"meters", 0.0)),
					desired,
				)
		return desired


	# Automatic policies select only prediction-aware display participants.
	func _can_automatically_simulate(member: NetwEntity) -> bool:
		if not member.prediction.is_registered():
			return false
		return member.prediction.sim_mode == PredictionHandle.SimMode.DISPLAY \
				or not member.prediction._simulation_subjects.is_empty()


	# Keeps a deferred handoff inside the nearest policy's structural budget.
	func _enforce_nearest_budget(
			committed: Dictionary[NetwEntity, bool],
	) -> void:
		var promotion: Dictionary = _handle.island_config.get(&"promotion", { })
		if promotion.get(&"kind", &"") != &"nearest":
			return
		var fidelity: Dictionary = _handle.island_config.get(&"fidelity", { })
		var automatic: Array[NetwEntity] = []
		for member: NetwEntity in committed:
			if int(fidelity.get(member, -1)) \
					!= PredictionHandle.Fidelity.SIMULATED:
				automatic.append(member)
		automatic.sort_custom(
			func(a: NetwEntity, b: NetwEntity) -> bool:
				var a_retained := _simulated_members.has(a)
				var b_retained := _simulated_members.has(b)
				if a_retained != b_retained:
					return a_retained
				var da := _distance_squared(a)
				var db := _distance_squared(b)
				return da < db or is_equal_approx(da, db) \
						and _entity_id_less(a, b),
		)
		var count := int(promotion.get(&"count", 0))
		for index in range(count, automatic.size()):
			committed.erase(automatic[index])


	# Keeps up to count nearest members with a ten-percent exit margin.
	func _promote_nearest(
			members: Array[NetwEntity],
			count: int,
			out: Dictionary[NetwEntity, bool],
	) -> void:
		if count <= 0 or members.is_empty():
			return
		members.sort_custom(
			func(a: NetwEntity, b: NetwEntity) -> bool:
				var da := _distance_squared(a)
				var db := _distance_squared(b)
				return da < db or is_equal_approx(da, db) \
						and _entity_id_less(a, b),
		)
		var limit := mini(count, members.size())
		var cutoff := _distance_squared(members[limit - 1]) * 1.21
		var retained: Array[NetwEntity] = []
		for member: NetwEntity in members:
			if _simulated_members.has(member) \
					and _distance_squared(member) <= cutoff:
				retained.append(member)
		retained.sort_custom(_entity_id_less)
		for member: NetwEntity in retained:
			if out.size() >= count:
				break
			out[member] = true
		for member: NetwEntity in members:
			if out.size() >= count:
				break
			out[member] = true


	# Applies a ten-percent exit margin to radius promotion.
	func _promote_within(
			members: Array[NetwEntity],
			meters: float,
			out: Dictionary[NetwEntity, bool],
	) -> void:
		var enter_squared := meters * meters
		var exit_squared := enter_squared * 1.21
		for member: NetwEntity in members:
			var limit := exit_squared if _simulated_members.has(member) \
					else enter_squared
			if _distance_squared(member) <= limit:
				out[member] = true


	# Commits promotion claims on participant handles.
	func _apply_island_promotions(
			desired: Dictionary[NetwEntity, bool],
	) -> void:
		for member: NetwEntity in _simulated_members:
			if not desired.has(member) and is_instance_valid(member):
				member.prediction._set_simulated_by(_entity, false)
		var predictors: Dictionary = _handle.island_config.get(
			&"command_predictors",
			{ },
		)
		for member: NetwEntity in desired:
			member.prediction._set_simulated_by(
				_entity,
				true,
				predictors.get(member, Callable()),
			)
		_simulated_members = desired


	# Drops every promotion owned by this subject.
	func _clear_island_promotions() -> void:
		for member: NetwEntity in _simulated_members:
			if is_instance_valid(member):
				member.prediction._set_simulated_by(_entity, false)
		_simulated_members.clear()


	func _distance_squared(member: NetwEntity) -> float:
		return _entity_position(_entity).distance_squared_to(
			_entity_position(member),
		)


	func _entity_position(member: NetwEntity) -> Vector3:
		var root := member.owner if member else null
		if root is Node3D:
			return (root as Node3D).global_position
		if root is Node2D:
			var point := (root as Node2D).global_position
			return Vector3(point.x, point.y, 0.0)
		return Vector3.ZERO


	func _entity_id_less(a: NetwEntity, b: NetwEntity) -> bool:
		var a_id := String(a.entity_id)
		var b_id := String(b.entity_id)
		if a_id == b_id:
			return a.get_instance_id() < b.get_instance_id()
		return a_id < b_id


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
		var contacts: Array[NetwEntity] = participants
		if not _realized_contact_entities.is_empty():
			contacts = []
			contacts.assign(_realized_contact_entities.keys())
		for participant in contacts:
			if participant not in participants:
				return false
			var handle := participant.prediction as PredictionHandle
			if handle == null \
					or handle.sim_mode == PredictionHandle.SimMode.DISPLAY:
				return false
		return true


	# The declared participants that are still live entities.
	func _island_participants() -> Array[NetwEntity]:
		if not _island_members.is_empty():
			return _island_members.duplicate()
		var out: Array[NetwEntity] = []
		for entry in _handle.island_config.get(&"participants", []):
			var participant := entry as NetwEntity
			if participant and is_instance_valid(participant):
				out.append(participant)
		return out


	# Names the gap once when a contact is reported against no declared island at
	# all. Silence there would let an entity look in-domain forever while
	# colliding with bodies nobody ever claimed the peers agree about.
	func _report_island_gap() -> void:
		if _island_gap_reported or not _island_participants().is_empty():
			return
		_island_gap_reported = true
		push_warning(
			"Prediction: %s reported contact with no declared island. Declare the "
			% _entity.entity_id
			+ "bodies it touches through island(), or its contact "
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


	# Applies the newest authored input after the tick-rate governor admits it.
	func _predict_frame_step(timing: PredictTiming) -> void:
		_last_frame_transition_tick = timing.tick
		if _speculation_horizon_full():
			_handle.speculation_held_count += 1
			_send_command_frame()
			return
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


	# Captures the command lane while fallback displays authority state.
	func _fallback_author_tick(tick: int) -> void:
		_predict_author_tick(tick)


	# Authors one FRAME command without opening a speculative transition.
	func _fallback_author_frame_step(timing: PredictTiming) -> void:
		_last_frame_transition_tick = timing.tick
		var plan := predict_fold(
			_latest_input_tick,
			_last_driven_input_tick,
			timing.tick,
		)
		var label: int = plan[&"label"]
		var fresh: bool = plan[&"fresh"]
		_author_tape_entry(label, fresh)
		if fresh:
			_last_driven_input_tick = label
		_send_command_frame()


	# Authors one TICK command without running or journaling speculation.
	func _fallback_author_step(tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		_prepare_tick_tape(tick)
		_author_tape_entry(tick, true)
		_last_driven_input_tick = tick
		_send_command_frame()


	func _predict_step(delta: float, tick: int) -> void:
		var input := _canonical_input(_input_binding.snapshot_payload())
		_timeline.record_input(tick, input)
		_latest_input_tick = tick
		_input_binding.authored_tick = tick
		if _speculation_horizon_full():
			_handle.speculation_held_count += 1
			_send_command_frame()
			return
		# A TICK drive is its own transition, so the tape entry is degenerate:
		# index, label, and tick are the same number and every entry is fresh.
		# The lane codec implies contiguous indices, so a clock re-anchor that
		# gaps the tick sequence starts a new tape epoch instead of straddling it.
		_prepare_tick_tape(tick)
		_author_tape_entry(tick, true)
		_last_driven_input_tick = tick
		_record_drive(tick, tick, PredictionHandle.DriveKind.FRESH, input)
		_run(input, delta, tick, true)
		var state := _capture()
		_timeline.record_state(tick + 1, state)
		_close_journal_row(tick, state)
		# The row is closed before the send, so the frame's fingerprint section
		# can claim the transition it just drove rather than trailing by a tick.
		_send_command_frame()


	# Steps a remote participant through F with a substituted predicted command.
	func _simulated_step(delta: float, tick: int) -> void:
		_run(_predicted_command(tick), delta, tick, false)
		_handle.drive_seq += 1
		_handle.substituted_count += 1
		_mark_idle_drive(tick, PredictionHandle.DriveKind.MISSING)


	# Applies the same one-drive FRAME cadence used by the subject island.
	func _simulated_frame_step(timing: PredictTiming) -> void:
		if timing.tick <= _last_frame_transition_tick:
			return
		_last_frame_transition_tick = timing.tick
		_simulated_step(timing.delta, timing.tick)


	# Merges a per-participant predictor over the COAST zero-input baseline.
	func _predicted_command(tick: int) -> Dictionary:
		var command := _coast_command()
		var predictor := _handle._predicted_command_callable()
		if not predictor.is_valid():
			return _canonical_input(command)
		var predicted: Variant = predictor.call(_entity, tick)
		if predicted is Dictionary:
			for key: Variant in predicted:
				command[key] = predicted[key]
			return _canonical_input(command)
		if not _invalid_command_predictor_reported:
			_invalid_command_predictor_reported = true
			push_error(
				"Prediction island command predictor for %s must return a "
				% _entity.entity_id
				+ "Dictionary. COAST was used instead.",
			)
		return _canonical_input(command)


	# Builds the type-preserving zero command that defines COAST.
	func _coast_command() -> Dictionary:
		var out: Dictionary = { }
		for key: StringName in _input_binding.snapshot_payload():
			out[key] = _zero_value(_input_binding.node().get(key))
		return out


	func _zero_value(value: Variant) -> Variant:
		match typeof(value):
			TYPE_BOOL:
				return false
			TYPE_INT:
				return 0
			TYPE_FLOAT:
				return 0.0
			TYPE_STRING:
				return ""
			TYPE_STRING_NAME:
				return &""
			TYPE_VECTOR2:
				return Vector2.ZERO
			TYPE_VECTOR2I:
				return Vector2i.ZERO
			TYPE_VECTOR3:
				return Vector3.ZERO
			TYPE_VECTOR3I:
				return Vector3i.ZERO
			TYPE_VECTOR4:
				return Vector4.ZERO
			TYPE_VECTOR4I:
				return Vector4i.ZERO
			TYPE_COLOR:
				return Color(0.0, 0.0, 0.0, 0.0)
			TYPE_ARRAY:
				return []
			TYPE_DICTIONARY:
				return { }
		return value


	# Independently rebases a simulated remote on every reconstructed state row.
	func _on_simulated_state_frame(header: Dictionary) -> void:
		_stream_reconstructed = _stream_reconstructed \
				or bool(header.get("whole", true))
		if not _stream_reconstructed:
			return
		var payload: Dictionary = header.get("payload", { })
		if payload.is_empty():
			return
		var recv_tick := int(header.get("tick", -1))
		var iface := _iface()
		var current_tick := iface._current_tick() if iface else recv_tick
		var age_ticks := clampi(
			maxi(0, current_tick - recv_tick),
			0,
			_handle.max_restore_ticks,
		)
		var target := payload
		if _handle.snap_restore == PredictionHandle.RestoreMode.EXTRAPOLATED:
			target = _extrapolated_payload(
				payload,
				float(age_ticks) * _tick_delta,
			)
		var divergence := PredictionHandle.divergence_by_field(
			_capture(),
			target,
			_angle_fields,
			_handle.last_field_divergence,
		)
		_handle.is_reconciling = true
		_handle.corrections += 1
		_restore(target)
		_handle.is_reconciling = false
		_handle.state_evaluated.emit(recv_tick, -1, divergence, true)


	# Re-keys a TICK tape when its clock jumps past the contiguous lane.
	func _prepare_tick_tape(tick: int) -> void:
		if not _authored_tape.is_empty() \
				and int(_authored_tape.back().get("index", -1)) != tick - 1:
			_tape_epoch = (_tape_epoch + 1) & 0xFF
			_authored_tape.clear()
			_journal.clear(_tape_epoch)
			_forensic_sidecars.clear()
			_witness_verdicts.clear()
			_deferred_operator_states.clear()
			_operator_deferred_basis = -1
			_ack_of_acks = -1
		_next_tape_entry_index = tick


	# True once another speculative transition would exceed the bounded horizon.
	func _speculation_horizon_full() -> bool:
		_refresh_owner_ack_age()
		return _handle.ack_age_ticks >= ACK_AGE_MAX


	# Publishes the speculative span from the freshest proof on either lane.
	func _refresh_owner_ack_age() -> void:
		if _handle._schedule == PredictionHandle.Schedule.FRAME:
			_handle.ack_age_ticks = maxi(
				0,
				_next_tape_entry_index - _latest_authority_ack - 1,
			)
		else:
			_handle.ack_age_ticks = maxi(
				0,
				_last_driven_input_tick - _latest_authority_ack,
			)


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
		_latest_authority_ack = maxi(_latest_authority_ack, ack)
		_refresh_owner_ack_age()
		if _reseed_align_pending:
			if _reseed_epoch_confirmed:
				_finish_reseed_alignment(recv_tick, ack, payload)
			else:
				_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)
			return
		if ack <= _reseed_ignore_through:
			_handle.state_evaluated.emit(recv_tick, ack, 0.0, false)
			return
		_reseed_ignore_through = -1
		var frame_entry: Dictionary = { }
		var ack_label := ack
		var predicted: Dictionary
		if _handle._schedule == PredictionHandle.Schedule.FRAME:
			frame_entry = _authored_entry_at(ack)
			if frame_entry.is_empty():
				return
			ack_label = int(frame_entry.get("label", -1))
			predicted = _entry_history.state_at(ack + 1)
			if predicted.is_empty():
				return
			_handle.last_compare_staleness = 0
		else:
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
		var settled := false
		var meter := 0
		var domain := _journal.domain_at(ack)
		if _stream_reconstructed:
			# The row is read after the fingerprint pass, so a transition this
			# frame's own state frame was able to judge is judged by that verdict
			# rather than waiting a frame for its label to be worth reading. Both
			# reads are scalar: this runs on every authoritative frame of every
			# predicted entity, where materializing a row would allocate to deliver
			# two bytes.
			var exact_verdict := _exact_verdict_of(_journal.flags_at(ack))
			var verdict := evaluate(
				domain,
				exact_verdict,
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
			settled = domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN \
					or exact_verdict != ExactVerdict.UNJUDGED
			if settled:
				meter = measure(
					_handle.last_field_divergence,
					_meter_tolerances(domain),
				)
				# The exact predicate is authoritative in domain. Preserve its
				# binary verdict if a conservative quantizer bound overlaps one
				# adjacent canonical value.
				if corrected and meter == 0:
					meter = 1
				elif not corrected:
					meter = 0
		var attribution := _attribution_for(ack)
		if settled:
			_journal.mark_aligned_error(ack, divergence)
			if corrected:
				if _episode.is_empty() or int(_episode.get(&"state", -1)) \
						!= PredictionHandle.EpisodeState.OPEN:
					_open_episode(ack, attribution)
				else:
					_record_episode_divergence(ack)
				_handle.divergence_detected.emit(ack, attribution)
			_record_episode_comparison(ack, meter, not corrected)
			if corrected and _episode_budget_exhausted():
				_enter_fallback(ack)
				_handle.state_evaluated.emit(
					recv_tick,
					ack,
					divergence,
					false,
				)
				return
		if corrected and (_transport_pending() or _dissipate_pending()):
			_handle.state_evaluated.emit(
				recv_tick,
				ack,
				divergence,
				false,
			)
			return
		if corrected and _defer_operator_for_witness(
			recv_tick,
			ack,
			payload,
		):
			_handle.state_evaluated.emit(
				recv_tick,
				ack,
				divergence,
				false,
			)
			return
		if corrected:
			# Re-resolve from the handle so a runtime correction_mode change is live.
			_correction = PredictionHandle.resolve_correction_mode_for(
				_entity.owner,
				_handle.correction_mode,
			)
			var before := _capture()
			var correction_write: Dictionary = { }
			# A promoted recovery reports an unmeasurable pose error rather than
			# its measured one: past the escalation the recovery is no longer
			# entitled to claim it sits below the teleport tier, whatever the
			# measurement says.
			var escalated := _escalate_next
			var transported := _try_transport(
				predicted,
				payload,
				before,
				ack,
				escalated,
			)
			if transported.is_empty() and _try_dissipate(
					ack,
					meter,
					domain,
					escalated,
			):
				_handle.state_evaluated.emit(
					recv_tick,
					ack,
					divergence,
					false,
				)
				return
			_escalate_next = false
			_handle.is_reconciling = true
			_handle.corrections += 1
			var plan: Dictionary
			if not transported.is_empty():
				plan = {
					&"restore": transported[&"restore"],
					&"write": transported[&"restore"],
					&"teleport": false,
					&"skip": false,
				}
				_last_correction_teleported = false
				_restore(
					plan[&"restore"],
					NetwPredictJournal.Operator.TRANSPORT_DELTA,
					ack,
				)
				correction_write = plan[&"write"]
			else:
				var recovery_projection := guard_projection(
					_restore_projection,
					_handle.last_field_divergence,
					_handle.divergence_epsilon,
					_epsilon_overrides,
					_handle.ack_age_ticks,
					_handle.max_restore_ticks,
					_tick_delta,
				)
				# The recorded payload remains raw. Only the body write is staged.
				plan = recover(
					payload,
					_handle.resolved_recovery_policy(),
					_correction,
					_handle.snap_restore,
					recovery_projection,
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
					_out_of_domain_until >= 0 \
							and ack_label < _out_of_domain_until,
				)
				_last_correction_teleported = plan[&"teleport"]
				_track_recovery_convergence(
					escalated, plan, divergence, predicted, payload,
				)
				if not plan[&"skip"]:
					var operator := NetwPredictJournal.Operator.REBASE_EXACT
					if bool(plan[&"teleport"]):
						operator = NetwPredictJournal.Operator.FULL_CLOSURE
					elif _correction == PredictionHandle.CorrectionMode.SNAP \
							and _handle.snap_restore \
							== PredictionHandle.RestoreMode.EXTRAPOLATED \
							and not recovery_projection.is_empty():
						operator = NetwPredictJournal.Operator.REBASE_PROJECTED
					_restore(plan[&"restore"], operator, ack)
					correction_write = plan[&"write"]
			# Anchor the authoritative state at its keyed tick so a later packet
			# carrying the same ack compares against the corrected value, not the
			# stale prediction it just replaced. Without this a duplicate ack (the
			# server is input starved and re-sends the same ack) re-triggers this
			# correction every tick until the ack advances past the stale entry.
			if _handle._schedule == PredictionHandle.Schedule.TICK:
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
			if replays and _handle._schedule == PredictionHandle.Schedule.FRAME:
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

		if _handle._schedule == PredictionHandle.Schedule.FRAME:
			_timeline.trim_before(ack_label)
			_entry_history.trim_before(ack)
		else:
			_timeline.trim_before(ack)
		_handle.state_evaluated.emit(recv_tick, ack, divergence, corrected)


	# Builds the fixed per-field zero region for the comparison's own domain.
	func _meter_tolerances(
			domain: NetwPredictJournal.Domain,
	) -> Dictionary:
		var tolerances: Dictionary[StringName, float] = { }
		var node := _state_binding.node() if _state_binding else null
		if not is_instance_valid(node) or not _state_binding.set:
			return tolerances
		for field: NetwSyncSet.Field in _state_binding.set.fields:
			if _trigger_excludes.has(field.key):
				if _causal_fields.has(field.key) \
						and _epsilon_overrides.has(field.key):
						tolerances[field.key] = _epsilon_overrides[field.key]
				continue
			if _withheld_fields.has(field.key) \
					and _epsilon_overrides.has(field.key):
				tolerances[field.key] = _epsilon_overrides[field.key]
				continue
			if domain == NetwPredictJournal.Domain.OUT_OF_DOMAIN:
				tolerances[field.key] = float(_epsilon_overrides.get(
					field.key,
					_handle.divergence_epsilon,
				))
				continue
			if not field.quantizer:
				tolerances[field.key] = 0.0
				continue
			var type := NetwScriptModel.get_node_property_type(node, field.key)
			tolerances[field.key] = field.quantizer._max_error(type)
		return tolerances


	# What a divergence at [param transition] is charged to.
	#
	# The acknowledgement lane is where both peers' command hashes and environment
	# digests meet, so a charge it already made is the best answer available and
	# is read off the transition's own journal row. Failing that, a substituted
	# row is still decisive on its own: authority declared it ran a command the
	# owner never authored, and no further evidence could change that.
	#
	# Anything else is UNKNOWN rather than charged to the suspect that
	# happens to be tested last. A tolerance failure on an out-of-domain
	# transition is not a bug with an address, and naming one would turn the
	# attribution into a decoration.
	func _attribution_for(transition: int) -> NetwPredictJournal.Attribution:
		var charged := _journal.attribution_at(transition)
		if charged != NetwPredictJournal.Attribution.UNKNOWN:
			return charged
		if _handle.last_attributed_transition == transition:
			return _handle.last_attribution
		if _journal.flags_at(transition) & NetwPredictJournal.ROW_SUBSTITUTED:
			return NetwPredictJournal.Attribution.COMMAND
		return NetwPredictJournal.Attribution.UNKNOWN


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
		_close_journal_row(index, state)


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
				and _handle._schedule == PredictionHandle.Schedule.FRAME


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
				member._restore(
					member.transition_state_at(ack),
					NetwPredictJournal.Operator.REBASE_EXACT,
					ack,
					self,
				)
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
			_record_non_contraction()
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
			return
		var direction := _divergence_direction(predicted, payload)
		var direction_key: StringName = direction[&"key"]
		var comparable_sign := _last_recovery_sign \
				if direction_key == _last_recovery_direction else 0
		var verdict := escalation_after(
			_nonshrink_streak,
			comparable_sign,
			_last_recovery_divergence,
			divergence,
			direction[&"sign"],
		)
		_nonshrink_streak = verdict[&"streak"]
		_last_recovery_direction = direction_key
		_last_recovery_sign = verdict[&"sign"]
		_last_recovery_divergence = divergence
		_escalate_next = verdict[&"escalate"]


	# Forgets the recovery-convergence evidence. Called where the divergence
	# story restarts: a shrink to agreement, a teleport, a rewire, an epoch
	# bump, and the escalation the evidence just spent itself on.
	func _reset_recovery_trackers() -> void:
		_nonshrink_streak = 0
		_last_recovery_direction = StringName()
		_last_recovery_sign = 0
		_last_recovery_divergence = -1.0
		_escalate_next = false


	# The field and dominant axis of the divergence this recovery answers.
	func _divergence_direction(
			predicted: Dictionary,
			payload: Dictionary,
	) -> Dictionary:
		var dominant := StringName()
		var top := 0.0
		for field: StringName in _handle.last_field_divergence:
			var error := float(_handle.last_field_divergence[field])
			if error > top and predicted.has(field) and payload.has(field):
				top = error
				dominant = field
		if dominant == StringName():
			return { &"key": StringName(), &"sign": 0 }
		return delta_direction(dominant, _pose_delta(
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
			_mark_skipped(
				transition,
				int(queued.get("label", -1)),
			)


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
			_mark_skipped(transition, transition)
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


	# Seals a new skipped row or overlays supersession on retained evidence.
	func _mark_skipped(transition: int, label: int) -> void:
		if _journal.row_at(transition).is_empty():
			_journal.open(
				transition,
				label,
				PredictionHandle.DriveKind.MISSING,
				0,
			)
			_journal.mark_substituted(transition)
		else:
			_journal.mark_superseded(transition)


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
		var is_new := _journal.row_at(transition).is_empty()
		var raw_state := _capture_raw()
		var pre_state := _state_binding.canonicalize_payload(raw_state)
		var pre_fp := _state_fingerprint(pre_state)
		var raw_fp := raw_state_fingerprint(raw_state) if _raw_fp_enabled else 0
		var evidence_mask := NetwPredictJournal.EVIDENCE_RAW \
				if _raw_fp_enabled else 0
		var provenance := _pending_provenance.duplicate()
		var previous := _journal.row_at(transition - 1)
		_forensic_sidecars[transition] = {
			&"pre_bytes": _state_binding.canonical_bytes(pre_state),
			&"command_bytes": _input_binding.canonical_bytes(input),
			&"command_payload": input.duplicate(true),
		}
		while _forensic_sidecars.size() > TAPE_HISTORY_LIMIT:
			var sidecar_transitions := _forensic_sidecars.keys()
			sidecar_transitions.sort()
			_forensic_sidecars.erase(sidecar_transitions[0])
		_journal.open(
			transition,
			label,
			kind,
			NetwPredictJournal.fnv1a(_input_binding.canonical_bytes(input)),
			pre_fp,
			_state_family_fingerprints(pre_state),
			provenance,
			_base_topology_fingerprint(),
			raw_fp,
			evidence_mask,
		)
		if is_new:
			_pending_provenance = { }
			var previous_flags := int(previous.get(&"flags", 0))
			var comparable := not previous.is_empty() \
					and bool(previous_flags & NetwPredictJournal.ROW_CLOSED) \
					and not bool(previous_flags & NetwPredictJournal.ROW_SUBSTITUTED)
			if comparable \
					and int(previous.get(&"post_fp", 0)) != pre_fp \
					and int(provenance.get(&"write_id", 0)) == 0:
				_journal.mark_chain_broken(transition)
				_handle.chain_break_count += 1
		# The environment is fingerprinted before the drive runs, because what
		# attribution needs to know is the world the transition ran AGAINST. A
		# digest taken afterward would describe the world the transition helped
		# make, which cannot exonerate or convict it.
		_e_digest = _sample_environment()
		var sidecar: Dictionary = _forensic_sidecars.get(transition, { })
		sidecar[&"environment_samples"] = _sensor_samples.duplicate(true)
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


	func _capture_raw() -> Dictionary:
		return _state_binding.snapshot_payload()


	func _state_fingerprint(payload: Dictionary) -> int:
		return NetwPredictJournal.fnv1a(
			_state_binding.canonical_bytes(payload),
		)


	# Fingerprints pose, momentum, then controller and latch state separately.
	func _state_family_fingerprints(payload: Dictionary) -> PackedInt32Array:
		var family_payloads: Array[Dictionary] = [{ }, { }, { }]
		for key: StringName in payload:
			if not _state_family_of.has(key):
				continue
			family_payloads[_state_family_of[key]][key] = payload[key]
		var out := PackedInt32Array()
		for family: Dictionary in family_payloads:
			out.append(
				0 if family.is_empty() else NetwPredictJournal.fnv1a(
					_state_binding.canonical_bytes(family),
				),
			)
		return out


	# Seals the after-solve observation before sealing the produced state.
	func _close_journal_row(transition: int, state: Dictionary) -> void:
		var row := _journal.row_at(transition)
		if row.is_empty() \
				or int(row.get(&"flags", 0)) & NetwPredictJournal.ROW_CLOSED:
			return
		var solve := _sample_solve_evidence(
			int(row.get(&"topo_fp", 0)),
			transition,
		)
		_journal.mark_solve(
			transition,
			int(solve[&"topo_fp"]),
			int(solve[&"witness_fp"]),
			int(row.get(&"evidence_mask", 0)) | int(solve[&"evidence_mask"]),
			solve[&"detail"],
			int(solve[&"witness_class_bits"]),
		)
		_journal.close(
			transition,
			_state_fingerprint(state),
			_state_family_fingerprints(state),
		)
		var sidecar: Dictionary = _forensic_sidecars.get(transition, { })
		sidecar[&"post_bytes"] = _state_binding.canonical_bytes(state)
		_maybe_demote_for_breach(transition, solve)


	# Closes speculation on the same transition that realized the breach.
	func _maybe_demote_for_breach(transition: int, solve: Dictionary) -> void:
		if _role != PredictionHandle.Role.PREDICT or _fallback_latched \
				or _handle.breach_response \
						!= PredictionHandle.BreachResponse.DEMOTE \
				or _handle.witness_config.is_empty() \
				or not bool(solve.get(&"breach", false)):
			return
		_journal.mark_domain(transition, NetwPredictJournal.Domain.OUT_OF_DOMAIN)
		if _episode.is_empty() or int(_episode.get(&"state", -1)) \
				!= PredictionHandle.EpisodeState.OPEN:
			_open_episode(transition, NetwPredictJournal.Attribution.CONTACT)
		else:
			_episode[&"generator_row_copy"] = _pin_row(transition)
			_episode[&"attribution"] = NetwPredictJournal.Attribution.CONTACT
		var write_id := _next_operator_write_id
		_next_operator_write_id += 1
		_record_episode_write(
			write_id,
			NetwPredictJournal.Operator.DEMOTE,
			transition,
			int(solve.get(&"witness_fp", 0)),
			_entity.entity_id,
			true,
		)
		_episode[&"breach_transition"] = transition
		_episode[&"breach_witness"] = solve.get(&"detail", { }).duplicate(true)
		_sync_episode()
		# TICK closes before its ordinary lane send. Preserve the command that
		# produced the breach before rewire opens the author-only epoch.
		_send_command_frame()
		_enter_fallback(transition, true)


	# Fingerprints execution facts known before the solve begins.
	func _base_topology_fingerprint() -> int:
		var participants := PackedStringArray()
		for participant in _island_participants():
			participants.append(String(participant.entity_id))
		participants.sort()
		return fact_fingerprint({
			&"schedule": _handle._schedule,
			&"epoch": int(_handle.island_config.get(&"epoch", -1)),
			&"participants": participants,
		})


	# Samples and classifies the realized solve. An absent declaration is unknown.
	func _sample_solve_evidence(
			base_topology: int,
			transition: int,
	) -> Dictionary:
		var empty := {
			&"topo_fp": base_topology,
			&"witness_fp": 0,
			&"witness_class_bits": 0,
			&"evidence_mask": 0,
			&"detail": { },
		}
		var sampler := _handle.witness_config.get(&"contacts", Callable()) \
				as Callable
		_realized_contact_entities.clear()
		if not sampler.is_valid():
			return empty
		var sample := _normalize_witness_sample(sampler.call())
		if sample.is_empty():
			return empty
		var support := _declared_support_collider()
		var contact_classes: Array[int] = []
		var collider_classes := PackedStringArray()
		var collider_ids := PackedStringArray()
		var compared_contacts := PackedStringArray()
		var outside_boundary_ids := PackedStringArray()
		var island_participants := _island_participants()
		var witness_class_bits := 0
		for value: Variant in sample[&"colliders"]:
			var collider := value as Object
			var collider_entity := _contact_entity(collider)
			if collider_entity and collider_entity != _entity:
				_realized_contact_entities[collider_entity] = true
			var contact_class := _classify_collider(collider, support)
			if contact_class not in contact_classes:
				contact_classes.append(contact_class)
			var collider_type := collider.get_class()
			if collider_type not in collider_classes:
				collider_classes.append(collider_type)
			var collider_id := _collider_identity(collider)
			if collider_id not in collider_ids:
				collider_ids.append(collider_id)
			if _contact_breaches_boundary(
					collider,
					support,
					island_participants,
			) \
					and collider_id not in outside_boundary_ids:
				outside_boundary_ids.append(collider_id)
			var witness_class := _witness_class(collider, support)
			witness_class_bits |= witness_class
			var compared := "%s:%d" % [collider_id, witness_class]
			if compared not in compared_contacts:
				compared_contacts.append(compared)
		if contact_classes.is_empty():
			contact_classes.append(PredictionHandle.ContactClass.NONE)
		contact_classes.sort()
		collider_classes.sort()
		collider_ids.sort()
		compared_contacts.sort()
		outside_boundary_ids.sort()
		var sleeping := bool(sample[&"sleeping"])
		var woke := _has_previous_witness and _previous_witness_sleeping \
				and not sleeping
		_previous_witness_sleeping = sleeping
		_has_previous_witness = true
		var detail := {
			&"contact_classes": contact_classes,
			&"collider_classes": collider_classes,
			&"collider_ids": collider_ids,
			&"contact_bucket": contact_count_bucket(
				(sample[&"colliders"] as Array).size(),
			),
			&"sleeping": sleeping,
			&"woke": woke,
			&"solve_ordinal": transition,
			&"witness_class_bits": witness_class_bits,
			&"outside_boundary_ids": outside_boundary_ids,
			&"breach": not outside_boundary_ids.is_empty(),
			&"continuous": sample.get(&"continuous", { }).duplicate(true),
		}
		var body_facts := { }
		for key in [&"body_mode", &"collision_layer", &"collision_mask"]:
			if sample.has(key):
				body_facts[key] = sample[key]
		var topology := base_topology
		if not body_facts.is_empty():
			body_facts[&"base"] = base_topology
			topology = fact_fingerprint(body_facts)
		return {
			&"topo_fp": topology,
			&"witness_fp": fact_fingerprint({
				&"contacts": compared_contacts,
			}),
			&"witness_class_bits": witness_class_bits,
			&"breach": not outside_boundary_ids.is_empty(),
			&"evidence_mask": NetwPredictJournal.EVIDENCE_WITNESS,
			&"detail": detail,
		}


	# Accepts only the documented witness shape and reports a bad declaration once.
	func _normalize_witness_sample(value: Variant) -> Dictionary:
		if value is Dictionary:
			var sample := value as Dictionary
			var colliders: Variant = sample.get(&"colliders", null)
			var sleeping: Variant = sample.get(&"sleeping", null)
			if colliders is Array and typeof(sleeping) == TYPE_BOOL:
				for collider: Variant in colliders:
					if not collider is Object or not is_instance_valid(collider):
						return _report_invalid_witness()
				var continuous: Variant = sample.get(&"continuous", { })
				if continuous is Dictionary:
					return sample
		return _report_invalid_witness()


	func _report_invalid_witness() -> Dictionary:
		if not _invalid_witness_reported:
			_invalid_witness_reported = true
			push_error(
				"Prediction witness must return {colliders: Array, sleeping: bool}; "
				+ "continuous, when present, must be a Dictionary.",
			)
		return { }


	func _declared_support_collider() -> String:
		var ground: Variant = _sensor_samples.get(&"ground", { })
		if ground is Dictionary:
			var collider: Variant = (ground as Dictionary).get(&"collider", null)
			if collider is String or collider is StringName:
				return String(collider)
		return ""


	func _classify_collider(
			collider: Object,
			support: String,
	) -> PredictionHandle.ContactClass:
		if not support.is_empty() and _collider_identity(collider) == support:
			return PredictionHandle.ContactClass.DECLARED_SUPPORT
		if collider is AnimatableBody2D or collider is AnimatableBody3D \
				or collider is CharacterBody2D or collider is CharacterBody3D:
			return PredictionHandle.ContactClass.KINEMATIC_PROXY
		if _is_static_geometry(collider):
			return PredictionHandle.ContactClass.OTHER_STATIC
		if collider is RigidBody2D and (collider as RigidBody2D).freeze \
				or collider is RigidBody3D and (collider as RigidBody3D).freeze:
			return PredictionHandle.ContactClass.KINEMATIC_PROXY
		var entity := _contact_entity(collider)
		var iface := _iface()
		if entity and iface and iface._engines.has(entity):
			return PredictionHandle.ContactClass.PREDICTED_DYNAMIC
		return PredictionHandle.ContactClass.UNPREDICTED_DYNAMIC


	# Returns the peer-invariant class compared across the acknowledgement lane.
	func _witness_class(collider: Object, support: String) -> int:
		if not support.is_empty() and _collider_identity(collider) == support:
			return PredictionHandle.WitnessClass.SUPPORT
		if _is_static_geometry(collider):
			return PredictionHandle.WitnessClass.STATIC
		return PredictionHandle.WitnessClass.DYNAMIC_ENTITY


	# True when the solve consumed a stand-in the closure cannot reproduce.
	# Static world geometry is identical on every peer, so touching it is
	# in-boundary; only an un-stepped dynamic body is a stale stand-in.
	func _contact_breaches_boundary(
			collider: Object,
			support: String,
			island_participants: Array[NetwEntity],
	) -> bool:
		if not support.is_empty() and _collider_identity(collider) == support:
			return false
		if _is_static_geometry(collider):
			return false
		var entity := _contact_entity(collider)
		if not entity:
			return true
		if entity == _entity:
			return false
		if entity not in island_participants:
			return true
		return entity.prediction.sim_mode == PredictionHandle.SimMode.DISPLAY


	func _collider_identity(collider: Object) -> String:
		var node := collider as Node
		if _is_static_geometry(collider):
			return "path:%s" % node.get_path()
		var entity := _contact_entity(collider)
		if entity:
			return "entity:%s" % entity.entity_id
		return "path:%s" % node.get_path() if node else "object:%s" % \
				collider.get_class()


	# True for fixed world geometry, excluding movable static-body subclasses.
	# Tile and grid worlds collide through their own node classes, never a
	# StaticBody, so the class list must name them explicitly.
	func _is_static_geometry(collider: Object) -> bool:
		if collider is AnimatableBody2D or collider is AnimatableBody3D:
			return false
		if collider is StaticBody2D or collider is StaticBody3D:
			return true
		return collider is GridMap or collider is CSGShape3D \
				or collider is TileMap or collider is TileMapLayer


	# Resolves the entity a contact belongs to. A scene wrapper is a container,
	# not a body: its level geometry must never read as contact with the scene
	# entity itself.
	func _contact_entity(collider: Object) -> NetwEntity:
		var node := collider as Node
		var entity := NetwEntity.of(node) if node else null
		if entity and entity.owner is MultiplayerScene:
			return null
		return entity


	func _restore(
			payload: Dictionary,
			operator: NetwPredictJournal.Operator = NetwPredictJournal.Operator.NONE,
			basis: int = -1,
			provenance_owner: _PredictionEngine = null,
			evidence_free: bool = false,
	) -> void:
		_state_binding.apply_payload(payload)
		if operator == NetwPredictJournal.Operator.NONE:
			return
		var episode_owner := provenance_owner if provenance_owner else self
		var episode_id := 0
		if not episode_owner._episode.is_empty() \
				and int(episode_owner._episode.get(&"state", -1)) \
				== PredictionHandle.EpisodeState.OPEN:
			episode_id = int(episode_owner._episode.get(&"id", 0))
		var write_id := episode_owner._next_operator_write_id
		_pending_provenance = {
			&"episode": episode_id,
			&"write_id": write_id,
			&"operator": operator,
			&"basis": basis,
		}
		episode_owner._next_operator_write_id += 1
		var target := _entity.entity_id if _entity else StringName()
		episode_owner._record_episode_write(
			write_id,
			operator,
			basis,
			NetwPredictJournal.fnv1a(
				_state_binding.canonical_bytes(payload),
			),
			target,
			evidence_free,
		)


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
