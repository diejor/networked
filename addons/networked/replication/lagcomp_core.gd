## Public lag-compensation surface for one [MultiplayerTree], owned by
## [NetwMultiplayer].
##
## The session always has one. It is constructed inert with the
## [NetwMultiplayer] that owns it and activates when
## [method NetwMultiplayer.service_install] receives a
## [NetwLagCompensationConfig], so an unconfigured session degrades to safe
## no-op queries and cleanly opts out
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
## [constant NetwPropertySet.Record.RECORD_STATE], its own authoritative history, and
## [constant NetwPropertySet.Record.RECORD_INPUT], the controller's claim it replays to
## verify rather than believe. [method register_timeline] fires off state-set
## presence for that reason. A stream whose author is trusted outright records
## nothing, so a [constant NetwPropertySet.Record.RECORD_BROADCAST] set is display
## truth that lives entirely outside this boundary and never grows a timeline.
##
## [br][br][b]The input to state lifecycle[/b]
## [br]The owning client authors input every tick and predicts immediately, then
## ships that input toward the server stamped with its
## [member NetwPropertySetBinding.authored_tick]. The server consumes one input per
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
## [method NetwDisplayHandle.displayed_authoring_tick], the
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
## [member NetwPredictionHandle.sensors] and
## [member NetwPredictionHandle.island]
## declare, and [code]F[/code] is
## [member NetwPredictionHandle.simulate] run on the
## [enum NetwPredict.Schedule] cadence.
## [code]H[/code] is captured by the engine, and [code]O[/code] is declared by
## [member NetwPredictionHandle.witness_contacts].
## When a transition disagrees with authority,
## [signal NetwPredictionHandle.divergence_detected]
## charges the disagreement to one antecedent as a
## [enum NetwPredictJournal.Attribution], and a
## [enum NetwPredict.RecoveryPolicy] re-bases
## [code]S[/code].
##
## [br][br][b]The quantum[/b]
## [br]A transition is a fixed amount of simulated time, and two peers only mean
## the same thing by one when they advance the world by the same amount inside
## it. That is the antecedent no column can record, because it is the width of
## the transition rather than anything inside it.
## [codeblock]
## a body the game integrates       one drive, one transition. Exact by
##                                  construction, nothing to arm.
##
## a body the physics server        the server steps once per frame on its own
## integrates                       cadence, so a frame that emits no tick would
##                                  advance the world by an amount no transition
##                                  claims. Declaring
##                                  NetwPredict.Archetype.SOLVER_BODY holds that frame
##                                  instead: no tick, no step, no transition.
## [/codeblock]
## The cost is stated plainly. A peer holds only the frames it has to spare, so
## a peer whose physics cannot sustain
## [member ClockCore.tickrate] times
## [member ClockCore.physics_steps_per_tick] steps per wall second
## cannot hold anything and falls behind the authority it tracks, which
## [member ClockCore.simulation_behind_count] reports. Running behind a
## host that is itself below budget means a session in slow motion, which is
## what a single machine already does under load. It is the consistent version
## of that, not an extra tax.
## [br][br]This couples the meaning of a tick and nothing else. Deterministic
## lockstep couples the players' commands and costs a round trip of input
## latency before anything moves. Commands here stay speculative, stay
## substitutable, and keep flowing on a held frame, because holding simulated
## time must never hold input.
## [br][br][member NetwPredictStats.quantum_steps] is what a transition
## measured, [member NetwPredictStats.quantum_declared] is what it owed, and
## [member NetwPredictStats.quantum_faults] counts the difference. It is
## the one divergence cause a peer detects alone, before any comparison
## disagrees, and a cross-peer mismatch is charged to
## [constant NetwPredictJournal.Attribution.TOPOLOGY] rather than exhausting the
## ladder.
##
## [br][br][b]The recovery guarantee[/b]
## [br]One disturbance episode is the unit of recovery. Inside it, an operator
## must shrink the aligned error or spend finite evidence toward one
## full-closure restore. A restore projects only while the channel error stays
## inside the destination's projected error budget, and a divergence outside
## the declared domain restores the whole closure. Exhausting the episode's
## finite evidence enters bounded
## [constant NetwPredict.RecoveryPolicy.DELAY_CLOSED]
## quarantine and a journaled reseed. A resume serves its first comparison on
## probation — predicting and comparing without writing — because the quarantine
## proof is assembled from authority witnesses and a demoted entity makes no
## prediction for them to judge. The telemetry-only
## [constant NetwPredict.RecoveryPolicy.OBSERVE] policy writes nothing,
## so a persistent divergence may keep its report open without oscillating.
## [br][br]No repairing policy can produce a persistent oscillation
## [i]on fields the ladder can reach[/i]. The condition is not a hedge, it is
## the thing to check: a field that is withheld from the sub-teleport tier and
## that no forward model advances has no operator, and every recovery it raises
## is non-contractive on the field that raised it. Such a field is still legal
## and still reported — the wire-time report at
## [method NetwScriptModel.PropertyConfig.teleport_only] names it — but it can
## no longer spend an episode's evidence or cause a demotion: its
## non-contractions record
## [constant NetwPredict.OperatorOutcome.WITHHELD] and are counted, not
## charged. That is what keeps the guarantee true rather than nearly true, and
## it is why a declaration gap degrades to
## [constant NetwPredict.RecoveryPolicy.OBSERVE]-like reporting instead of
## to a worse outcome than declaring nothing at all.
##
## [br][br][b]The transparency bargain[/b]
## [br]The engine explains only what was declared. An undeclared environment
## read leaves its transitions
## [constant NetwPredictJournal.Domain.OUT_OF_DOMAIN] and its divergences
## [constant NetwPredictJournal.Attribution.UNKNOWN]. Declaring the
## environment through
## [member NetwPredictionHandle.sensors],
## [member NetwPredictionHandle.epoch], and
## [member NetwPredictionHandle.island] is
## how attribution sharpens from "something differed" to the antecedent that
## differed. An undeclared
## [member NetwPredictionHandle.witness_contacts]
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
## [br][br]The last of those is not available to every body, and the reason is
## the engine rather than this addon. Replaying [code]N[/code] transitions means
## running [code]N[/code] solves inside one frame, and the physics server offers
## no way to step a space on demand, so the reachable count per frame is zero or
## one. A body the game integrates can be replayed, because the game owns
## [code]F[/code] and can re-run it. A body the physics server integrates cannot
## be, at any configuration, which is why
## [constant NetwPredict.Reconcile.JOINT] stays reserved for islands whose
## members step themselves.
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
class_name LagCompCore
extends RefCounted

const PredictionCore := preload("res://addons/networked/replication/prediction_core.gd")

const DebugFeature := preload("res://addons/networked/debug/ui/debug_feature.gd")

## One drive pass's timing.
##
## [i]Deprecated.[/i] The record is [NetwPredict.Timing] now. It moved to the
## vocabulary leaf so the kernel can name the type without depending on the
## shell that steps it.
const PredictTiming := NetwPredict.Timing

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
## [member ClockCore.recommended_display_offset] for the network and jitter
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
## api._lagcomp.peer_divergence.connect(
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
var _clock: ClockCore
# Physics frames this session has run. A transition's cost in simulated time is
# the difference between the frames two consecutive drives ran on, and that cost
# has to match on every peer for a compared transition to mean anything.
var _physics_frame: int = 0
# The clock configuration a quantum report has already judged, so the report
# fires once per distinct configuration rather than once per drive.
var _quantum_config_reported: int = -1
# Entities whose archetype says the physics server integrates their body, each
# with the space it last resolved into so a release can restore what it held.
#   { NetwEntity -> { space: RID, dimension: int } }
var _gated_entities: Dictionary = { }
# Re-stepping drivers by physics space RID. A space named here can replay its
# own steps, and one absent from the map cannot.
var _steppers: Dictionary = { }
# Relay subscribers per entity slot, server-side.
var _relay_book := NetwPredictRelayBook.new()


func install_stepper(space: RID, stepper: NetwPhysicsStepper = null) -> void:
	if stepper == null or not stepper._can_step():
		_steppers.erase(space)
		return
	_steppers[space] = stepper


func stepper_for(space: RID) -> NetwPhysicsStepper:
	return _steppers.get(space) as NetwPhysicsStepper


## Admits [param peer] to [param entity]'s relayed command lane, or drops it
## when [param subscribed] is false.
##
## The interest gate is re-answered on every relay rather than remembered here,
## so a peer that leaves the entity's interest set stops receiving its commands
## without anything having to observe the exit. This returns the verdict for the
## request itself, which is what a subscriber learns.
##
## [br][br][b]Server Only.[/b]
func relay_subscribe(
		entity: NetwEntity,
		peer: int,
		subscribed: bool = true,
) -> Error:
	var api := _api()
	if api == null or not api.is_server():
		return ERR_UNAUTHORIZED
	if entity == null or not is_instance_valid(entity):
		return ERR_DOES_NOT_EXIST
	var slot := api._entity_slots.slot_of(entity)
	if slot == 0:
		return ERR_DOES_NOT_EXIST
	if not subscribed:
		_relay_book.set_subscribed(slot, peer, false)
		return OK
	if not api.interest_admits(entity.rid, peer):
		return ERR_UNAUTHORIZED
	_relay_book.set_subscribed(slot, peer, true)
	return OK


## Returns whether [param peer] holds an admitted subscription to
## [param entity]'s relayed command lane.
func relay_subscribed(entity: NetwEntity, peer: int) -> bool:
	var api := _api()
	if api == null or entity == null or not is_instance_valid(entity):
		return false
	return _relay_book.subscribed(api._entity_slots.slot_of(entity), peer)


# Re-emits one admitted command frame to every subscriber interest still
# admits, byte for byte. A subscriber that decoded a re-cut frame would be
# reading a command its author never wrote, so the payload is passed through
# rather than re-encoded, and the author is skipped because it already has it.
func _relay_command_frame(
		entity: NetwEntity,
		payload: PackedByteArray,
		author: int,
) -> void:
	var api := _api()
	if api == null or not api.is_server():
		return
	var slot := api._entity_slots.slot_of(entity)
	var subscribers := _relay_book.peers(slot)
	if subscribers.is_empty():
		return
	var liveness := api._liveness if api else null
	var route := liveness.route_of(entity) if liveness else -1
	if route <= 0:
		return
	for peer in subscribers:
		if peer == author:
			continue
		if not api.interest_admits(entity.rid, peer):
			_relay_book.set_subscribed(slot, peer, false)
			continue
		api._replication.send_to(
			peer,
			route,
			NetwFrameEnvelope.Channel.PREDICT_RELAY,
			payload,
			false,
		)

var _registry := _TimelineRegistry.new()
var _recorder := _HistoryRecorder.new()
var _runner := _SimulationRunner.new()
# Per-entity prediction engine records, created by register_prediction, keyed by
# NetwEntity. The handle on NetwEntity.prediction reaches its record back through
# a weakref, so erasing an entry here is the whole release.
var _engines: Dictionary = { }
var _prediction_pool := NetwPredictionEngine.new()
var _prediction_slots: Dictionary[NetwEntity, int] = { }
# One acknowledgement run judges a whole window of transitions, and the pool
# copies the row it is handed, so the carrier is refilled rather than reminted.
var _peer_evidence := NetwPredictEvidence.new()


# The two questions an engine asks about its siblings, named so they are a
# contract rather than a reach into this file's storage.
#
# An island rollback re-runs every declared member together, and a contact
# classifier asks whether the body it touched is one this peer predicts. Both
# are questions about the registry, which the shell owns; neither is a question
# an engine can answer from its own state. Spelled here, an engine never depends
# on how the registry is stored, and this file stays free to change that.
func engine_for(entity: NetwEntity) -> PredictionCore._PredictionEngine:
	return _engines.get(entity) as PredictionCore._PredictionEngine


func native_prediction_slot(entity: NetwEntity) -> int:
	return int(_prediction_slots.get(entity, -1))


# True when [param entity] has a prediction engine on this peer, which is what
# makes a contacted body predicted rather than merely replicated.
func predicts(entity: NetwEntity) -> bool:
	return _engines.has(entity)


const _PredictionBoundaryOverlay := preload(
	"res://addons/networked/debug/prediction_boundary_overlay.gd"
)
var _prediction_overlays: Dictionary[NetwEntity, Node] = { }

# Env-gated JSONL drain of the public prediction surface, built lazily the
# first frame it is armed and never re-checked once found off. The every-N
# gate is safe while N stays under the journal ring depth, because the tap's
# export cursor guarantees no sealed row is ever skipped.
const _PredictTap := preload("res://addons/networked/replication/netw_predict_tap.gd")
const _TAP_EVERY_ENV := "NETW_PREDICT_TAP_EVERY"
var _tap = null
var _tap_off: bool = false
var _tap_every: int = 1
var _tap_frame: int = 0
var _queries: _RewindQueries
var _pending_actions: Array[_PendingAction] = []
var _action_slots: Dictionary[String, int] = { }
var _observed_entities: Dictionary[NetwEntity, bool] = { }
var _gate_fallbacks: int = 0


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	_queries = _RewindQueries.new(_registry)
	_runner._service = self


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


## Applies [param config] to the engine. Called by [NetwMultiplayer] when a
## [NetwMultiplayer] installs a [NetwLagCompensationConfig]. The values live
## here, not on the node, so [method is_configured] stays true after a scene
## change frees the configurator.
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
static func resolve_required(node: Node) -> LagCompCore:
	# Resolve the session api directly, falling back to the enclosing tree when
	# a node's own multiplayer is not yet bound to the api at call time.
	var api := NetwMultiplayer.of(node)
	if api == null:
		var mt := MultiplayerTree.resolve(node)
		api = mt.api if mt else null
	if not api:
		return null
	if api._lagcomp.is_configured():
		return api._lagcomp
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
	var engine := PredictionCore._PredictionEngine.new()
	_engines[entity] = engine
	_prediction_slots[entity] = _prediction_pool.open(null)
	_hold_slot(entity)
	entity.prediction._engine_ref = weakref(engine)
	engine._attach(self, entity)
	_attach_prediction_overlay(entity)


# The declaration model an engine wires on, resolved for [param entity] here
# because both set handles resolve through the replication registry and the
# liveness route, and both axes read this peer's authority. This is the one
# place a prediction engine's wiring depends on a live session.
func declaration_of(entity: NetwEntity) -> PredictionCore.Declaration:
	var declaration := PredictionCore.Declaration.new()
	if not entity:
		return declaration
	declaration.state = entity.state_binding
	declaration.input = entity.input_binding
	declaration.authority = entity.is_authority
	declaration.controlled_locally = entity.is_controlled_locally
	return declaration


## Releases [param entity]'s prediction engine record, restoring the set-handle
## hooks it held. [member NetwEntity.prediction] stays bound and keeps its config
## and counters, so a re-registration resumes where the engine left off.
func unregister_prediction(entity: NetwEntity) -> void:
	var engine := _engines.get(entity) as PredictionCore._PredictionEngine
	if not engine:
		return
	_engines.erase(entity)
	var native_slot := int(_prediction_slots.get(entity, -1))
	if native_slot >= 0:
		_prediction_pool.close(native_slot)
	_prediction_slots.erase(entity)
	_release_slot(entity)
	_detach_prediction_overlay(entity)
	engine._release()
	entity.prediction._engine_ref = null


func configure_native_prediction(
		entity: NetwEntity,
		binding: NetwPropertySetBinding,
		input_binding: NetwPropertySetBinding,
		schedule: int,
		role: int,
		declared_correction: int,
		restore: int,
		max_restore_ticks: int,
		island: int,
		carry: bool,
		witness: bool,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0 or not binding or not binding.set:
		return -1
	var node := binding.node()
	var declaration := NetwPredictDeclaration.new()
	for field: NetwPropertySet.Column in binding.set.columns:
		declaration.append_field(
			field.key,
			field.property_class,
			binding.carry_channel_of(field.key),
			binding.converge_stiffness_of(field.key),
			binding.teleport_only_of(field.key),
			binding.reconcile_only_of(field.key),
			binding.epsilon_override_of(field.key),
			binding.teleport_at_of(field.key),
			false,
			field.quantizer,
			_declared_property_type(node, field.key),
		)
	_prediction_pool.rewire(slot, declaration, _input_declaration(input_binding))
	_bind_declared_owner(entity, node)
	var correction: int = _prediction_pool.resolve_correction(
		slot,
		declared_correction,
	)
	if not _prediction_pool.configure(
		slot,
		schedule,
		role,
		correction,
		restore,
		max_restore_ticks,
		island,
		carry,
		witness,
	):
		return -1
	return correction


# The plane's owner is the object that holds the declared properties. The
# simulate step is adopted from it, falling back to the entity root, because a
# component may carry the property set while the root carries the body.
func _bind_declared_owner(entity: NetwEntity, node: Node) -> void:
	if not is_instance_valid(node):
		return
	var api := _api()
	if api == null:
		return
	api.predict_bind_owner(entity.rid, node)
	var handle := entity.prediction
	if handle == null:
		return
	if not handle.simulate.is_valid():
		var root := entity.owner
		if is_instance_valid(root) and root.has_method(&"_network_tick"):
			handle.simulate = Callable(root, &"_network_tick")
	native_set_order_key(entity, _api()._liveness.route_of(entity))
	native_set_simulate(entity, handle.simulate)
	native_set_witness(entity, handle.witness_contacts)
	native_set_corridor(entity, handle.transport_corridor)
	for name: StringName in handle.sensors:
		native_set_sensor(entity, name, handle.sensors[name])


# An input row is canonicalized and shipped, never recovered, so the pool needs
# its codec columns and none of the recovery declarations.
func _input_declaration(
		binding: NetwPropertySetBinding,
) -> NetwPredictDeclaration:
	if not binding or not binding.set:
		return null
	var node := binding.node()
	var declaration := NetwPredictDeclaration.new()
	for field: NetwPropertySet.Column in binding.set.columns:
		declaration.append_field(
			field.key,
			field.property_class,
			&"",
			0.0,
			false,
			false,
			-1.0,
			-1.0,
			false,
			field.quantizer,
			_declared_property_type(node, field.key),
		)
	return declaration


func _declared_property_type(node: Node, key: StringName) -> int:
	if not is_instance_valid(node):
		return TYPE_NIL
	return NetwScriptModel.get_node_property_type(node, key)


func configure_native_prediction_axes(
		entity: NetwEntity,
		schedule: int,
		role: int,
		correction: int,
		restore: int,
		max_restore_ticks: int,
		island: int,
		carry: bool,
		witness: bool,
) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.configure(
		slot,
		schedule,
		role,
		correction,
		restore,
		max_restore_ticks,
		island,
		carry,
		witness,
	) if slot >= 0 else false


func native_bind_owner(entity: NetwEntity, owner: Object) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.bind_owner(slot, owner) if slot >= 0 else false


func native_unbind_owner(entity: NetwEntity) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.unbind_owner(slot)


func native_owner_bound(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.owner_bound(slot) if slot >= 0 else false


func native_owner_solves(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.owner_solves(slot) if slot >= 0 else false


func native_apply_state(entity: NetwEntity, payload: Dictionary) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.apply_state(slot, payload) if slot >= 0 else false


func native_capture_state(entity: NetwEntity) -> Dictionary:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.capture_state(slot) if slot >= 0 else { }


func native_set_order_key(entity: NetwEntity, route: int) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_order_key(slot, route if route > 0 else -1)


func native_set_simulate(entity: NetwEntity, callback: Callable) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_simulate(slot, callback)


func native_set_witness(entity: NetwEntity, callback: Callable) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_witness(slot, callback)


func native_set_corridor(entity: NetwEntity, callback: Callable) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_corridor(slot, callback)


func native_set_sensor(
		entity: NetwEntity,
		name: StringName,
		callback: Callable,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_sensor(slot, name, callback)


func native_set_carry(
		entity: NetwEntity,
		field: StringName,
		callback: Callable,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.set_carry(slot, field, callback)


func native_judge_carry(
		entity: NetwEntity,
		field: StringName,
		same_type: bool,
		finite: bool,
		within_envelope: bool,
		pure: bool,
		faithful: bool,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return NetwPredictionEngine.CARRY_DECLINED
	return _prediction_pool.judge_carry(
		slot,
		field,
		same_type,
		finite,
		within_envelope,
		pure,
		faithful,
	)


func native_decline_carry(entity: NetwEntity, field: StringName) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return NetwPredictionEngine.CARRY_DECLINED
	return _prediction_pool.decline_carry(slot, field)


func native_record_episode_decision(
		entity: NetwEntity,
		operator: int,
		basis: int,
		eligible: bool,
		applied: bool,
		eligibility: Dictionary,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return
	_prediction_pool.record_episode_decision(
		slot,
		operator,
		basis,
		eligible,
		applied,
		eligibility,
	)


func native_reseed_align_pending(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.reseed_align_pending(slot) if slot >= 0 else false


func native_reseed_epoch_confirmed(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.reseed_epoch_confirmed(slot) if slot >= 0 else false


func native_confirm_reseed_epoch(entity: NetwEntity) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.confirm_reseed_epoch(slot)


func native_adopt_alignment(
		entity: NetwEntity,
		transition: int,
		ignore_through: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.adopt_alignment(slot, transition, ignore_through)


func native_admit_post_reseed(entity: NetwEntity, basis: int) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.admit_post_reseed(slot, basis) if slot >= 0 \
			else true


func native_probation_pending(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.probation_pending(slot) if slot >= 0 else false


func native_finish_probation(entity: NetwEntity, corrected: bool) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.finish_probation(slot, corrected) if slot >= 0 \
			else false


func native_open_episode(
		entity: NetwEntity,
		transition: int,
		attribution: int,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	return _prediction_pool.open_episode(slot, transition, attribution)


func native_record_episode_divergence(
		entity: NetwEntity,
		transition: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.record_episode_divergence(slot, transition)


func native_record_episode_escalation(
		entity: NetwEntity,
		trigger_shape: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.record_episode_escalation(slot, trigger_shape)


func native_stamp_episode_write_delta(
		entity: NetwEntity,
		delta_fp: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.stamp_episode_write_delta(slot, delta_fp)


func native_record_episode_write(
		entity: NetwEntity,
		operator: int,
		basis: int,
		delta_fp: int,
		target: StringName,
		ack_age: int,
		trigger_shape: int,
		evidence_free: bool = false,
		null_operator: bool = false,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return 0
	return _prediction_pool.record_episode_write(
		slot,
		operator,
		basis,
		delta_fp,
		target,
		ack_age,
		trigger_shape,
		evidence_free,
		null_operator,
	)


func native_episode_stats(entity: NetwEntity) -> PackedInt64Array:
	return _prediction_pool.episode_stats(native_prediction_slot(entity))


func native_episode_active(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.episode_active(slot) if slot >= 0 else false


func native_episode_state(entity: NetwEntity) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.episode_state(slot) if slot >= 0 else -1


func native_episode_budget_exhausted(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.episode_budget_exhausted(slot) if slot >= 0 \
			else false


func native_episode_operator_pending(
		entity: NetwEntity,
		operator: int,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	return _prediction_pool.episode_operator_pending(slot, operator)


func native_record_breach(entity: NetwEntity, transition: int) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.record_breach(slot, transition)


func native_episode(entity: NetwEntity) -> NetwPredictEpisodeReport:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.episode(slot) if slot >= 0 else null


func native_witness_row_clean(
		entity: NetwEntity,
		transition: int,
		require_peer: bool,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	return _prediction_pool.witness_row_clean(slot, transition, require_peer)


func native_resolve_correction(entity: NetwEntity, declared: int) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return declared
	return _prediction_pool.resolve_correction(slot, declared)


func native_transport_admissible(
		entity: NetwEntity,
		candidate: bool,
		basis_witness_clean: bool,
		recent_witness_clean: bool,
		non_pose_agrees: bool,
		below_teleport: bool,
		escalated: bool,
		observing: bool,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	return _prediction_pool.transport_admissible(
		slot,
		candidate,
		basis_witness_clean,
		recent_witness_clean,
		non_pose_agrees,
		below_teleport,
		escalated,
		observing,
	)


func native_dissipate_admissible(
		entity: NetwEntity,
		momentum_active: bool,
		other_active: bool,
		basis_witness_clean: bool,
		recent_witness_clean: bool,
		escalated: bool,
		observing: bool,
		meter: int,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	return _prediction_pool.dissipate_admissible(
		slot,
		momentum_active,
		other_active,
		basis_witness_clean,
		recent_witness_clean,
		escalated,
		observing,
		meter,
	)


func native_dissipate_declared(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.dissipate_declared(slot) if slot >= 0 else false


func native_static_geometry(collider: Object) -> bool:
	return _prediction_pool.static_geometry(collider)


func native_witness_class(collider: Object, declared_support: bool) -> int:
	return _prediction_pool.witness_class(collider, declared_support)


func native_carry_eligible(entity: NetwEntity, field: StringName) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.carry_eligible(slot, field) if slot >= 0 else false


func native_carry_retired(entity: NetwEntity, field: StringName) -> bool:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.carry_retired(slot, field) if slot >= 0 else false


func native_carry_infidelity(entity: NetwEntity, field: StringName) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return 0
	return int(_prediction_pool.carry_stats(slot, field)[2])


func native_run_step(
		entity: NetwEntity,
		input: Dictionary,
		delta: float,
		tick: int,
		is_fresh: bool,
) -> Dictionary:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.run_step(
		slot,
		input,
		delta,
		tick,
		is_fresh,
	) if slot >= 0 else { }


func native_capture_input(entity: NetwEntity) -> Dictionary:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.capture_input(slot) if slot >= 0 else { }


func native_canonicalize_state(
		entity: NetwEntity,
		payload: Dictionary,
) -> Dictionary:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.canonicalize_state(slot, payload) if slot >= 0 \
			else payload.duplicate()


func native_canonicalize_input(
		entity: NetwEntity,
		payload: Dictionary,
) -> Dictionary:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.canonicalize_input(slot, payload) if slot >= 0 \
			else payload.duplicate()


func native_canonical_state_bytes(
		entity: NetwEntity,
		payload: Dictionary,
) -> PackedByteArray:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.canonical_state_bytes(slot, payload) if slot >= 0 \
			else PackedByteArray()


func native_canonical_input_bytes(
		entity: NetwEntity,
		payload: Dictionary,
) -> PackedByteArray:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.canonical_input_bytes(slot, payload) if slot >= 0 \
			else PackedByteArray()


func native_record_input(entity: NetwEntity, tick: int, c_hash: int) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.record_input(slot, tick, c_hash)


func native_plan_consume_input(
		schedule: int,
		has_input: bool,
		has_later_input: bool,
		has_last_input: bool,
		missing_policy: int,
) -> NetwPredictConsumePlan:
	return _prediction_pool.plan_consume_input(
		schedule,
		has_input,
		has_later_input,
		has_last_input,
		missing_policy,
	)


# Returns the pool's own transition for the drive it opened, or -1 when it
# opened none. A FRAME slot numbers its transitions by tape index, which is not
# the number the shell files the same drive under, so the caller keys its close
# on what this returns rather than on what it passed in.
func native_open_drive(
		entity: NetwEntity,
		topology: Dictionary,
		tick: int,
		frame: int,
		ticktime: float,
		quantum: int,
		simulating: bool,
		pre_fp: int,
		families: PackedInt32Array,
		raw_fp: int = 0,
		evidence_mask: int = 0,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0 or families.size() < 3:
		return -1
	var drive := _prediction_pool.open_drive(
		slot,
		topology,
		tick,
		frame,
		ticktime,
		quantum,
		simulating,
		pre_fp,
		families[0],
		families[1],
		families[2],
		raw_fp,
		evidence_mask,
	)
	return drive.transition() if drive.ran() else -1


# Hands the pool the after-solve observation, as the facts the shell read off
# live bodies rather than as the fingerprints it folded them into. The fold is
# the pool's, so a peer that observes the same world files the same evidence
# whichever side folded it.
func native_record_evidence(
		entity: NetwEntity,
		transition: int,
		environment_epoch: int,
		environment: Dictionary,
		topology: Dictionary,
		contacts: Array[Dictionary],
		sleeping: bool,
) -> bool:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return false
	var identities := PackedStringArray()
	var witness_classes := PackedInt32Array()
	var realizations := PackedInt32Array()
	var outside_boundary := PackedByteArray()
	for contact: Dictionary in contacts:
		identities.append(String(contact[&"identity"]))
		witness_classes.append(int(contact[&"witness_class"]))
		realizations.append(int(contact[&"realization"]))
		outside_boundary.append(1 if bool(contact[&"outside_boundary"]) else 0)
	return _prediction_pool.record_evidence(
		slot,
		transition,
		environment_epoch,
		environment,
		topology,
		identities,
		witness_classes,
		realizations,
		outside_boundary,
		sleeping,
	)


# Journals a transition the owner authored and this peer is replaying. Returns
# the pool's transition, which a replay always names itself, so the caller keys
# its close the same way an authored drive does.
func native_replay_drive(
		entity: NetwEntity,
		topology: Dictionary,
		transition: int,
		label: int,
		kind: int,
		tick: int,
		frame: int,
		ticktime: float,
		quantum: int,
		pre_fp: int,
		families: PackedInt32Array,
		raw_fp: int = 0,
		evidence_mask: int = 0,
) -> int:
	var slot := native_prediction_slot(entity)
	if slot < 0 or families.size() < 3:
		return -1
	var drive := _prediction_pool.replay_drive(
		slot,
		topology,
		transition,
		label,
		kind,
		tick,
		frame,
		ticktime,
		quantum,
		pre_fp,
		families[0],
		families[1],
		families[2],
		raw_fp,
		evidence_mask,
	)
	return drive.transition() if drive.ran() else -1


func native_close_drive(
		entity: NetwEntity,
		transition: int,
		post_fp: int,
		families: PackedInt32Array,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot < 0 or families.size() < 3:
		return
	_prediction_pool.close_drive(
		slot,
		transition,
		post_fp,
		families[0],
		families[1],
		families[2],
	)


# Records whether [param transition] was entitled to exactness. The pool stores
# the answer and never derives it, because whether a world was declared, whether
# it was declared approximate, and how long a change to it keeps the window open
# are facts only the shell holds.
func native_mark_domain(
		entity: NetwEntity,
		transition: int,
		domain: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_domain(slot, transition, domain)


func native_mark_chain_broken(
		entity: NetwEntity,
		transition: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_chain_broken(slot, transition)


func native_mark_provenance(
		entity: NetwEntity,
		transition: int,
		provenance: Dictionary,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_provenance(
			slot,
			transition,
			int(provenance.get(&"episode", 0)),
			int(provenance.get(&"write_id", 0)),
			int(provenance.get(&"operator", 0)),
			int(provenance.get(&"basis", -1)),
		)


func native_mark_attribution(
		entity: NetwEntity,
		transition: int,
		attribution: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_attribution(slot, transition, attribution)


func native_mark_differing_family(
		entity: NetwEntity,
		transition: int,
		family: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_differing_family(slot, transition, family)


func native_mark_witness_match(
		entity: NetwEntity,
		transition: int,
		matched: bool,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_witness_match(slot, transition, matched)


# Records the error a comparison that reached a verdict measured. A comparison
# that reached none writes nothing, so the column never carries a zero that
# would read as agreement.
func native_mark_aligned_error(
		entity: NetwEntity,
		transition: int,
		error: float,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.mark_aligned_error(slot, transition, error)


# Journals a transition authority declared it never ran. The consume path
# numbers a replayed transition by the entry rather than by a pass of its own,
# so a skipped one needs no translation into the pool's numbering.
func native_declare_skipped(
		entity: NetwEntity,
		transition: int,
		label: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.declare_skipped(slot, transition, label)


# Drops every retained native row and adopts the shell's tape epoch.
func native_journal_clear(entity: NetwEntity, epoch: int) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.journal_clear(slot, epoch)


func native_tape_reset(entity: NetwEntity, epoch: int) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.tape_reset(slot, epoch)


# Returns the newest native row whose drive produced a state.
func native_journal_last_closed(entity: NetwEntity) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.journal_last_closed(slot) if slot >= 0 else -1


func native_journal_epoch(entity: NetwEntity) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.journal_epoch(slot) if slot >= 0 else -1


# Returns the retained transition indices in oldest-first journal order.
func native_journal_transitions(entity: NetwEntity) -> PackedInt64Array:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.journal_transitions(slot) \
			if slot >= 0 else PackedInt64Array()


# Returns one detached native journal row, or null after eviction.
func native_journal_row(
		entity: NetwEntity,
		transition: int,
) -> NetwPredictJournalRow:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.journal_row(slot, transition) \
			if slot >= 0 else null


# Builds authority's bounded acknowledgement prefix from the native journal.
func native_build_ack_frame(
		entity: NetwEntity,
		epoch: int,
		ack: int,
		owner_ack_floor: int,
) -> PackedByteArray:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return PackedByteArray()
	return _prediction_pool.build_ack_frame(
		slot,
		epoch,
		ack,
		owner_ack_floor,
	)


# One acknowledgement stages one recovery, so the carrier is refilled rather
# than reminted.
var _recovery_request := NetwPredictRecoveryRequest.new()


func native_escalation_pending(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return slot >= 0 and _prediction_pool.escalation_pending(slot)


# The pool owns the ladder, the recovery window and the cooldown, so a caller
# hands over the two states and the request facts and takes back the whole
# write plan rather than the parts of one.
func native_recover(
		entity: NetwEntity,
		predicted: Array,
		authority: Array,
		current: Array,
		tier_errors: PackedFloat64Array,
		basis: int,
		current_label: int,
		policy: int,
		fallback_epsilon: float,
		fallback_teleport: float,
		max_restore_ticks: int,
		ack_age_ticks: int,
		collision_cooldown_ticks: int,
		tick_delta: float,
		domain: int,
		attribution: int,
		contact_window: bool,
		suppressed: bool,
		pose_unmeasured: bool,
) -> NetwPredictWritePlan:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return null
	_recovery_request.fill(
		predicted,
		authority,
		current,
		tier_errors,
		basis,
		current_label,
		policy,
		fallback_epsilon,
		fallback_teleport,
		max_restore_ticks,
		ack_age_ticks,
		collision_cooldown_ticks,
		tick_delta,
		domain,
		attribution,
		contact_window,
		suppressed,
		pose_unmeasured,
	)
	return _prediction_pool.recover(slot, _recovery_request)


# The pool latches its own quarantine from a comparison it judged, so a caller
# that latched for a reason no comparison reaches enters it here. Fallback is a
# state of an open episode, so [param attribution] opens one when the pool
# holds none. The call is idempotent against a slot already latched in
# fallback.
func native_enter_quarantine(
		entity: NetwEntity,
		transition: int,
		stream_reconstructed: bool,
		attribution: int,
		demoted: bool = false,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return
	_prediction_pool.enter_quarantine(
		slot,
		transition,
		stream_reconstructed,
		attribution,
		demoted,
	)


func native_quarantine_latched(entity: NetwEntity) -> bool:
	var slot := native_prediction_slot(entity)
	return slot >= 0 and _prediction_pool.quarantine_latched(slot)


func native_quarantine_target(entity: NetwEntity) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.quarantine_target(slot) if slot >= 0 else 0


func native_quarantine_clean_run(entity: NetwEntity) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.quarantine_clean_run(slot) if slot >= 0 else 0


# The state and the acknowledgement lanes arrive independently and either one
# can complete the proof, so both return the seed plan rather than a signal
# that one is available.
func native_quarantine_state(
		entity: NetwEntity,
		tick: int,
		basis: int,
		payload: Array,
		whole: bool,
) -> NetwPredictWritePlan:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return null
	return _prediction_pool.quarantine_state(slot, tick, basis, payload, whole)


func native_quarantine_witness(
		entity: NetwEntity,
		basis: int,
		bits: int,
) -> NetwPredictWritePlan:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return null
	return _prediction_pool.quarantine_witness(slot, basis, bits)


# Commits [param owner]'s produced roster and returns the members the pool
# promoted, in the order the pool named them. The columns are one row per
# member of [param members]: its declared fidelity, its squared distance from
# the owner, whether it can be simulated automatically, and whether the owner's
# body is touching it this transition.
#
# A member the pool has no slot for is dropped rather than committed, because
# an island the pool cannot step is not a group it can replay.
func native_island_commit(
		owner: NetwEntity,
		owner_order_key: int,
		members: Array[NetwEntity],
		order_keys: PackedInt64Array,
		fidelities: PackedInt32Array,
		distance_squared: PackedFloat64Array,
		eligible: PackedByteArray,
		contact: PackedByteArray,
		promotion: int,
		promotion_count: int,
		promotion_meters: float,
		frontier: int,
) -> Array[NetwEntity]:
	var promoted: Array[NetwEntity] = []
	var owner_slot := native_prediction_slot(owner)
	if owner_slot < 0 or _prediction_pool.island_of(owner_slot) \
			== NetwPredictionEngine.ISLAND_NONE:
		return promoted
	var slots := PackedInt64Array()
	var kept_keys := PackedInt64Array()
	var kept_fidelities := PackedInt32Array()
	var kept_distances := PackedFloat64Array()
	var kept_eligible := PackedByteArray()
	var kept_contact := PackedByteArray()
	var by_slot: Dictionary[int, NetwEntity] = { }
	for at: int in members.size():
		var slot := native_prediction_slot(members[at])
		if slot < 0 or slot == owner_slot or by_slot.has(slot):
			continue
		slots.append(slot)
		kept_keys.append(order_keys[at])
		kept_fidelities.append(fidelities[at])
		kept_distances.append(distance_squared[at])
		kept_eligible.append(eligible[at])
		kept_contact.append(contact[at])
		by_slot[slot] = members[at]
	for slot: int in _prediction_pool.island_commit(
			owner_slot,
			owner_order_key,
			slots,
			kept_keys,
			kept_distances,
			kept_fidelities,
			kept_eligible,
			kept_contact,
			promotion,
			promotion_count,
			promotion_meters,
			frontier,
	):
		promoted.append(by_slot[slot])
	return promoted


# Files one member's replay cell: the state the transition produced and the
# command that drove it, with the three facts the pool ranks provenance by. A
# record carrying only a better command passes an empty [param state], which
# the pool reads as no observation rather than as an erased one.
func native_joint_record(
		entity: NetwEntity,
		transition: int,
		state: Array,
		command: Variant,
		authored: bool,
		relayed: bool,
		predictor_valid: bool,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot < 0 or transition < 0:
		return
	_prediction_pool.joint_record(
		slot,
		transition,
		state,
		command,
		authored,
		relayed,
		predictor_valid,
	)


func native_joint_note_basis(
		entity: NetwEntity,
		basis: int,
		source: int,
) -> void:
	var slot := native_prediction_slot(entity)
	if slot < 0 or basis < 0 or _prediction_pool.island_of(slot) \
			== NetwPredictionEngine.ISLAND_NONE:
		return
	_prediction_pool.joint_note_basis(slot, basis, source)


# What drove one transition on [param entity], as the provenance the pool
# ranks cells by, or -1 for a transition it holds no cell for.
func native_joint_provenance(entity: NetwEntity, transition: int) -> int:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.joint_provenance_at(slot, transition) \
			if slot >= 0 else -1


func native_joint_command(entity: NetwEntity, transition: int) -> Variant:
	var slot := native_prediction_slot(entity)
	return _prediction_pool.joint_command_at(slot, transition) \
			if slot >= 0 else null


# A re-keyed epoch renumbers every transition, so the cells retained across it
# name transitions that no longer exist.
func native_joint_clear(entity: NetwEntity) -> void:
	var slot := native_prediction_slot(entity)
	if slot >= 0:
		_prediction_pool.joint_clear(slot)


func native_joint_pass(
		owner: NetwEntity,
		present: int,
) -> NetwPredictJointPlan:
	var slot := native_prediction_slot(owner)
	if slot < 0 or _prediction_pool.island_of(slot) \
			== NetwPredictionEngine.ISLAND_NONE:
		return null
	return _prediction_pool.joint_pass(slot, present)


# The pool judges from its own journal row and its own state, so the caller
# hands over the two rows and the tolerances and takes back a verdict rather
# than the parts of one.
func native_compare_state(
		entity: NetwEntity,
		recv_tick: int,
		transition: int,
		predicted: Array,
		authority: Array,
		correction_tolerances: PackedFloat64Array,
		meter_tolerances: PackedFloat64Array,
		fallback_epsilon: float,
		stream_reconstructed: bool,
		ack_domain_confirmed: bool,
) -> NetwPredictVerdict:
	var slot := native_prediction_slot(entity)
	if slot < 0:
		return null
	return _prediction_pool.compare_state(
		slot,
		recv_tick,
		transition,
		predicted,
		authority,
		correction_tolerances,
		meter_tolerances,
		fallback_epsilon,
		stream_reconstructed,
		ack_domain_confirmed,
	)


# Admits one authority acknowledgement, as the columns the far peer recorded
# rather than as a verdict the caller reached. Comparing is the pool's, so two
# peers holding the same two rows name the same antecedent whichever of them
# ran the comparison.
#
# [param complete] states that the run carried every column for this
# transition. An incomplete run can still say the post-states differ, which is
# why it is admitted at all, but it can never name what differed.
func native_admit_ack(
		entity: NetwEntity,
		transition: int,
		pre_fp: int,
		c_hash: int,
		e_digest: int,
		post_fp: int,
		pre_families: PackedInt32Array,
		post_families: PackedInt32Array,
		topo_fp: int,
		witness_fp: int,
		raw_fp: int,
		evidence_mask: int,
		complete: bool,
		substituted: bool,
) -> NetwPredictVerdict:
	var slot := native_prediction_slot(entity)
	if slot < 0 or pre_families.size() < 3 or post_families.size() < 3:
		return null
	_peer_evidence.fill(
		pre_fp,
		c_hash,
		e_digest,
		post_fp,
		pre_families[0],
		pre_families[1],
		pre_families[2],
		post_families[0],
		post_families[1],
		post_families[2],
		topo_fp,
		witness_fp,
		raw_fp,
		evidence_mask,
		complete,
	)
	return _prediction_pool.admit_ack(
		slot,
		transition,
		_peer_evidence,
		substituted,
	)


# A predicting engine is released only by an explicit predict_undeclare, which
# is a longer hold than interest's tree-exit retirement. Claiming the slot is
# what stops interest's commit sweeping a number this core still reads.
const _SLOT_HOLDER := &"predict"


func _hold_slot(entity: NetwEntity) -> void:
	var api := _api()
	if api:
		api._entity_slots.ensure(entity, _SLOT_HOLDER)


func _release_slot(entity: NetwEntity) -> void:
	var api := _api()
	if api:
		# The subscribers go with the slot, because a slot is never reused and a
		# row left behind would answer for an entity nothing predicts.
		_relay_book.release(api._entity_slots.slot_of(entity))
		api._entity_slots.release(entity, _SLOT_HOLDER)


# Adds the read-only in-world overlay when the shared debug gate is active.
func _attach_prediction_overlay(entity: NetwEntity) -> void:
	if not DebugFeature.is_world_debug_enabled():
		return
	if not bool(
		ProjectSettings.get_setting(
			"debug/networked/prediction_boundary_overlay",
			true,
		),
	):
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
##   `- gate_fallbacks: int    # state-ready actions resolved best-effort
## }
## [/codeblock]
func metrics() -> Dictionary:
	var result := _runner.metrics()
	result[&"timelines"] = _registry.size()
	result[&"pending_actions"] = _pending_actions.size()
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
## its children. The returned action predicts through the session's effect
## ledger ([method NetwMultiplayer.effect_arm]) and uses the mounted
## [LagCompensation] node for private request transport.
func action(authority: Callable) -> NetwAction:
	var slot := _assign_action_slot(authority) if _configured else 0
	return NetwAction.new(self, authority, slot)


## Advances the simulation loop by one tick: drains admitted actions, steps every
## registered prediction engine record, and on the server records authoritative
## history. Driven by the [LagCompensation] configurator's
## [signal ClockCore.on_tick] binding.
func tick_step(delta: float, tick: int) -> void:
	_drain_pending_actions(tick)
	_runner.tick_step(
		NetwPredict.Timing.new(
			tick,
			delta,
			_ticktime(),
			_physics_frame,
			_declared_quantum(),
		),
	)
	# The server holds the truth, so only it records authoritative history.
	var api := _api()
	if _configured and api and api.is_server():
		_recorder.record_tick(_registry, _engines, tick)


## Records the state produced by the preceding FRAME drive after its physics
## solve has completed. Driven before the next frame's tick loop.
func before_frame_step() -> void:
	# One solve completed since the last call, unless the clock held it. The
	# clock's decision still names the frame that just ended here, because this
	# runs before the tick loop resolves the frame now opening, so a held frame
	# costs a transition nothing and every other frame costs it one.
	if not is_instance_valid(_clock) or _clock.is_simulating:
		_physics_frame += 1
	_runner.before_frame_step()
	var api := _api()
	if _configured and api and api.is_server():
		_recorder.record_frame(_registry, _engines, _current_tick())
	_drain_tap()


## Advances every FRAME-scheduled simulation once after the clock's tick loop.
## Tick callbacks only author and label input for these entities. This pass
## performs their one drive application for the physics frame.
##
## The pass closes by flushing the transport, because it is the only producer
## that runs after the tick loop and so the only one whose frames would otherwise
## wait for a flush it already missed. See
## [method ReplicationCore.on_frame_end].
func frame_step() -> void:
	_runner.frame_step(frame_timing())
	_apply_simulation_gate()
	var api := _api()
	if _configured and api:
		api._replication.on_frame_end()


# Holds or admits every gated body's space for the solve this frame is about to
# run. Called after the drives and before the physics server steps, which is the
# only window where the decision can still take effect.
func _apply_simulation_gate() -> void:
	if _gated_entities.is_empty():
		return
	var active := not is_instance_valid(_clock) or _clock.is_simulating
	var stale: Array[NetwEntity] = []
	for entity: NetwEntity in _gated_entities:
		if not is_instance_valid(entity) or not is_instance_valid(entity.owner):
			stale.append(entity)
			continue
		var record: Dictionary = _gated_entities[entity]
		var resolved := _entity_space(entity)
		if resolved[&"space"] != record[&"space"]:
			# A reparent moved the body to another world. Give the world it left
			# its own clock back before adopting the new one.
			_set_space_active(record, true)
			_gated_entities[entity] = resolved
			record = resolved
		_set_space_active(record, active)
	for entity: NetwEntity in stale:
		_gated_entities.erase(entity)
	if _gated_entities.is_empty() and is_instance_valid(_clock):
		while _clock.is_gated():
			_clock.release_gate()


# The physics space a predicted body actually steps in, with the server that
# owns it. Resolved from the node rather than from any viewport it sits under,
# because those are two different questions and only this one names the space.
func _entity_space(entity: NetwEntity) -> Dictionary:
	var node := entity.owner if entity else null
	if node is Node3D:
		var world_3d := (node as Node3D).get_world_3d()
		if world_3d:
			return { &"space": world_3d.space, &"dimension": 3 }
	elif node is CanvasItem:
		var world_2d := (node as CanvasItem).get_world_2d()
		if world_2d:
			return { &"space": world_2d.space, &"dimension": 2 }
	return { &"space": RID(), &"dimension": 0 }


func _set_space_active(record: Dictionary, active: bool) -> void:
	var space: RID = record[&"space"]
	if not space.is_valid():
		return
	match int(record[&"dimension"]):
		3:
			PhysicsServer3D.space_set_active(space, active)
		2:
			PhysicsServer2D.space_set_active(space, active)


# Arms or releases [param entity]'s gate as its declaration resolves. A gate is
# armed by the archetype that says the physics server integrates this body, so a
# game whose bodies it does not integrate never holds a frame.
func _sync_simulation_gate(entity: NetwEntity, wanted: bool) -> void:
	if not entity:
		return
	var held := _gated_entities.has(entity)
	if wanted == held:
		return
	if wanted:
		_gated_entities[entity] = _entity_space(entity)
		if is_instance_valid(_clock):
			_clock.arm_gate()
		return
	# Releasing must give the space back, because nothing else will: an
	# unarmed clock stops resolving and a held space would stay held forever.
	_set_space_active(_gated_entities[entity], true)
	_gated_entities.erase(entity)
	if is_instance_valid(_clock):
		_clock.release_gate()


# Drains every registered entity's public evidence to JSONL when the
# NETW_PREDICT_TAP directory is set. The tap reads only the public handle, so
# it never moves recorded state, and it meters its own wall cost.
func _drain_tap() -> void:
	if _tap_off:
		return
	if _tap == null:
		if not _PredictTap.armed():
			_tap_off = true
			return
		_tap = _PredictTap.new()
		_tap_every = maxi(
			1,
			OS.get_environment(_TAP_EVERY_ENV).to_int(),
		)
	_tap_frame += 1
	if _tap_frame % _tap_every != 0:
		return
	for entity: NetwEntity in _engines:
		if is_instance_valid(entity):
			_tap.drain(entity.entity_id, entity.prediction)


## Returns the prediction tap's self-reported cost, or an empty [Dictionary]
## while no tap is armed.
##
## The tap is the one instrument whose price once masqueraded as a game
## defect, so its cost is a first-class read a capture harness echoes beside
## the numbers the tap produced: bytes and lines written, drain calls, and
## the mean wall cost of one drain.
func tap_cost() -> Dictionary:
	return _tap.cost() if _tap else { }


## Flushes the prediction tap's buffered lines to disk, so a capture collected
## while the session still runs reads complete files. A no-op while no tap is
## armed. The tap flushes once per second on its own and closes with the
## session, so most readers never need this.
func flush_tap() -> void:
	if _tap:
		_tap.flush()


# Closes the tap with the session, which flushes its tail and prints its
# self-reported cost. Driven by the LagCompensation configurator's removal.
func _close_tap() -> void:
	if _tap:
		_tap.close()
		_tap = null


## Reads the clock once for one FRAME pass and returns it by value.
##
## A frame drive completes the solve of the tick before the one now opening, so
## the transition it authors is labeled [code]tick - 1[/code]. Callers that step
## an engine directly through
## [method NetwPredictionHandle.simulate_frame] capture
## here too, so a direct drive and a pumped drive agree about when they are.
func frame_timing() -> PredictTiming:
	if not is_instance_valid(_clock):
		return NetwPredict.Timing.new(0, 0.0, 0.0, _physics_frame, 1)
	return NetwPredict.Timing.new(
		_clock.tick - 1,
		_clock.ticktime,
		_clock.ticktime,
		_physics_frame,
		_declared_quantum(),
		_clock.is_simulating,
	)


# The fixed network tick duration, or 0.0 when no clock is resolvable. A reader
# that gets 0.0 keeps whatever step it already had rather than adopting a
# meaningless one.
func _ticktime() -> float:
	return _clock.ticktime if is_instance_valid(_clock) else 0.0


# Physics steps one transition is declared to advance. The physics server runs
# exactly one step per frame, so this is whole by construction wherever the
# declaration is sound, and _report_quantum_misconfiguration says so when it is
# not.
func _declared_quantum() -> int:
	if not is_instance_valid(_clock):
		return 1
	return maxi(1, int(round(_clock.physics_factor)))


# A body the physics server integrates needs a whole number of steps per
# transition, because the server runs exactly one step per frame. A fractional
# ratio makes the count alternate on a phase each peer keeps privately, so two
# peers can never spend the same simulated time on the same transition and no
# other declaration can repair it.
func _report_quantum_misconfiguration(entity: NetwEntity) -> void:
	if not is_instance_valid(_clock):
		return
	var config := hash([_clock.tickrate, Engine.physics_ticks_per_second])
	if config == _quantum_config_reported:
		return
	_quantum_config_reported = config
	var factor: float = _clock.physics_factor
	if is_equal_approx(factor, roundf(factor)):
		return
	push_error(
		(
				"Prediction: %s drives a solver body at %d physics steps per "
				+ "second against a tickrate of %d, so one transition costs "
				+ "%.3f steps. The physics server runs exactly one step per "
				+ "frame, so a fractional cost alternates on a phase each peer "
				+ "keeps privately and the two never advance the same simulated "
				+ "time. Make physics_ticks_per_second a whole multiple of "
				+ "tickrate."
		) % [
			entity.entity_id if is_instance_valid(entity) else &"entity",
			Engine.physics_ticks_per_second,
			_clock.tickrate,
			factor,
		],
	)


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
	var liveness := api._liveness if api else null
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
	var liveness := api._liveness if api else null
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
			api._replication.send_to(
				MultiplayerPeer.TARGET_PEER_SERVER,
				route,
				NetwFrameEnvelope.Channel.ACTION,
				payload,
				true,
			)
		return
	submit_action(route, method, view_tick, data, key, timing_mode, 0)


func _deny_action_to(requester: int, key: StringName) -> void:
	var api := _api()
	var transport := api if _configured and api \
			and api.multiplayer_peer else null
	var local_peer := transport.get_unique_id() if transport else 0
	var remote := transport and requester != 0 and requester != local_peer \
			and requester in transport.get_peers()
	if remote:
		transport._replication.send_to(
			requester,
			0,
			NetwFrameEnvelope.Channel.LAGCOMP_DENY,
			var_to_bytes(key),
			true,
		)
		return
	if api:
		api.effect_discard(key)


# Client receive for a denied action. Discards the optimistic effect keyed by
# the denial.
func _handle_deny(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var api := _api()
	if api:
		api.effect_discard(bytes_to_var(payload))


func _handle_predict_command_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var engine := _engines.get(entity) as PredictionCore._PredictionEngine
	if engine:
		engine.receive_command_frame(payload)
	# Relayed after the engine consumed it, so a subscriber never sees a
	# command authority itself refused.
	_relay_command_frame(entity, payload, sender)


func _handle_predict_relay_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as PredictionCore._PredictionEngine
	if engine:
		engine.receive_relayed_command_frame(payload)


func _handle_predict_relay_request_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		sender: int,
) -> void:
	var request := NetwPredictRelayBook.request_of(payload)
	if request < 0:
		return
	relay_subscribe(entity, sender, request == 1)


func _handle_predict_ack_carrier(
		entity: NetwEntity,
		payload: PackedByteArray,
		_sender: int,
) -> void:
	var engine := _engines.get(entity) as PredictionCore._PredictionEngine
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
	var engine := _engines.get(entity) as PredictionCore._PredictionEngine
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
	var api := _api()
	if api and not entity.entity_id.is_empty():
		api.effect_adopt(entity.entity_id)
	if _observed_entities.has(entity):
		return
	_observed_entities[entity] = true
	if not entity.spawned.is_connected(_on_entity_spawned):
		entity.spawned.connect(_on_entity_spawned.bind(entity))


func _on_entity_spawned(entity: NetwEntity) -> void:
	var api := _api()
	if api and entity and not entity.entity_id.is_empty():
		api.effect_adopt(entity.entity_id)


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
	# Keyed by the RefCounted NetwEntity so there is no node-path coupling. The
	# value is the pool slot; the NetwTimeline behind it is the pool's, because
	# the prediction plane reads the same rows this recorder writes and two
	# stores would be two histories that agree only by accident.
	var _slots: Dictionary[NetwEntity, int] = { }
	var _core := NetwLagCompCore.new()


	# Registers entity, returning its NetwTimeline. Idempotent: a repeat call
	# returns the existing timeline and publishes it to NetwEntity.timeline.
	func register(entity: NetwEntity) -> NetwTimeline:
		if not entity:
			return null
		var existing := of(entity)
		if existing:
			return existing
		var slot := _core.timeline_open(NetwTimeline.DEFAULT_LIMIT)
		_slots[entity] = slot
		var tl := _core.timeline_history(slot)
		entity.timeline = tl
		return tl


	func of(entity: NetwEntity) -> NetwTimeline:
		var slot := int(_slots.get(entity, -1))
		return _core.timeline_history(slot) if slot >= 0 else null


	# The entity's recorded state at or before tick, read out of the pool that
	# holds it rather than out of a second copy.
	func sample(entity: NetwEntity, tick: int) -> Dictionary:
		var slot := int(_slots.get(entity, -1))
		return _core.timeline_sample(slot, tick) if slot >= 0 else { }


	func unregister(entity: NetwEntity) -> void:
		var slot := int(_slots.get(entity, -1))
		if slot >= 0:
			_core.timeline_close(slot)
		_slots.erase(entity)


	# Count of registered rewindable entities, read by the debug monitor.
	func size() -> int:
		return _slots.size()


	# The live NetwEntity to NetwTimeline map, iterated by the recorder each tick.
	func all() -> Dictionary[NetwEntity, NetwTimeline]:
		var out: Dictionary[NetwEntity, NetwTimeline] = { }
		for entity: NetwEntity in _slots:
			out[entity] = _core.timeline_history(int(_slots[entity]))
		return out


## The one reading of the clock a single simulation pass gets, taken at the pump
## boundary and handed down by value to everything the pass drives.
##
## A prediction kernel decides from its antecedents alone, so it may not resolve
## a clock of its own. Two kernels in one pass that each asked would be free to
## disagree about which tick they were running, and a replay could reproduce
## neither answer. Capturing once makes the pass's timing an antecedent like the
## command and the previous state, which is what lets
## [method LagCompCore.frame_step] drive a body with no clock in
## reach at all.
## [codeblock]
## clock ──> tick_step / frame_step  ── NetwPredict.Timing ──> engine ──> kernels
##             (the only reader)         (by value)      (no clock reference)
## [/codeblock]
# Steps every registered prediction engine each tick in the order the pool
# declares, so the server consumes every entity identically each run and a
# replayed trace is reproducible. Capability logic stays in the engine record;
# this owns only ordering and metric aggregation.
class _SimulationRunner extends RefCounted:
	var _engines: Array[PredictionCore._PredictionEngine] = []
	# The pool's order, resolved back to engines and rebuilt only when the
	# roster changes, so re-resolving an unchanged roster every tick is not
	# paid.
	var _sorted: Array[PredictionCore._PredictionEngine] = []
	var _sort_dirty: bool = true
	var _service: LagCompCore


	func register(engine: PredictionCore._PredictionEngine) -> void:
		if engine not in _engines:
			_engines.append(engine)
			_sort_dirty = true


	func unregister(engine: PredictionCore._PredictionEngine) -> void:
		_engines.erase(engine)
		_sort_dirty = true


	func tick_step(timing: NetwPredict.Timing) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.prepare_island(NetwPredict.Schedule.TICK)
		if _sort_dirty:
			_rebuild_sorted()
		# Every group replays before any member drives fresh, so a pass carries
		# the whole group to the present against one committed roster.
		for engine in _sorted:
			engine.joint_pass(timing)
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.network_tick(timing)


	func frame_step(timing: NetwPredict.Timing) -> void:
		if _sort_dirty:
			_rebuild_sorted()
		for engine in _sorted:
			engine.prepare_island(NetwPredict.Schedule.FRAME)
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
		var joint := _joint_metrics()
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			corrections += handle.stats.corrections
			max_replay = maxi(max_replay, handle.stats.max_replay_depth)
			consumed += handle.stats.consumed
			missing += handle.stats.missing
			folded += handle.stats.folded
		return {
			&"entities": _engines.size(),
			&"corrections": corrections,
			&"max_replay_depth": max_replay,
			&"consumed": consumed,
			&"missing": missing,
			&"folded": folded,
			&"joint": joint,
		}


	# The replay groups' own cadence, summed over the engines that ran a pass.
	# A group that never replayed reports zeros rather than being absent, so a
	# reader can tell a quiet cadence from an unreported one.
	#
	# The depth histogram and the floor-move breakdown are per-group shapes
	# rather than scalars, so they stay on the member's own
	# [NetwPredictStats] where a handle reads them keyed to one group.
	func _joint_metrics() -> Dictionary:
		var passes := 0
		var members := 0
		var relayed := 0
		var substituted := 0
		var heals := 0
		var lingering := 0
		for engine in _engines:
			var handle := engine.handle()
			if not handle:
				continue
			passes += handle.stats.joint_passes
			members = maxi(members, handle.stats.joint_members)
			relayed += handle.stats.cells_relayed
			substituted += handle.stats.cells_substituted
			heals += handle.stats.heal_snaps
			lingering += handle.stats.linger_held
		return {
			&"joint_passes": passes,
			&"joint_members": members,
			&"cells_relayed": relayed,
			&"cells_substituted": substituted,
			&"heal_snaps": heals,
			&"linger_held": lingering,
		}


	# Stable order by entity id so the server consumes every entity identically each
	# run. Rebuilt only when an engine registers or unregisters, since entity ids
	# are fixed once spawned.
	func _rebuild_sorted() -> void:
		_sorted = _pool_order()
		if _sorted.size() != _engines.size():
			_sorted = _engines.duplicate()
			_sorted.sort_custom(
				func(
						a: PredictionCore._PredictionEngine,
						b: PredictionCore._PredictionEngine,
				) -> bool:
					return a.order_key() < b.order_key()
			)
		_sort_dirty = false


	# The pool holds the order key, so it holds the order. An engine the pool
	# does not name is an engine mid-registration, and the caller falls back to
	# sorting the roster it has rather than stepping a partial one.
	func _pool_order() -> Array[PredictionCore._PredictionEngine]:
		var resolved: Array[PredictionCore._PredictionEngine] = []
		if _service == null:
			return resolved
		var by_slot: Dictionary[int, PredictionCore._PredictionEngine] = { }
		for engine in _engines:
			var slot := _service.native_prediction_slot(engine._entity)
			if slot < 0:
				return []
			by_slot[slot] = engine
		for slot: int in _service._prediction_pool.ordered_slots():
			var engine := by_slot.get(slot) as PredictionCore._PredictionEngine
			if engine:
				resolved.append(engine)
		return resolved


# Records every registered entity's authoritative state snapshot after a tick. The
# server holds the truth, so the recorder runs only on server authority and reads
# each entity's state-set snapshot through NetwEntity.state_binding. This gives
# non-predicted state-synced entities rewind history too, without a prediction
# engine.
class _HistoryRecorder extends RefCounted:
	# Records the current snapshot_payload of every entity in registry into its
	# timeline at tick. A consuming engine keys its record at the input-backed tick
	# through PredictionCore._PredictionEngine.history_record_tick.
	func record_tick(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
	) -> void:
		_record(
			registry,
			engines,
			tick,
			NetwPredict.Schedule.TICK,
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
			NetwPredict.Schedule.FRAME,
			false,
		)


	func _record(
			registry: _TimelineRegistry,
			engines: Dictionary,
			tick: int,
			schedule: NetwPredict.Schedule,
			include_unregistered: bool,
	) -> void:
		var timelines := registry.all()
		for entity in timelines:
			if not is_instance_valid(entity.owner):
				continue
			# A deactivated entity (a lingering despawn) freezes its history at the
			# despawn boundary instead of recording stale frozen copies, so its
			# retained window ages from the moment it died and expires cleanly when
			# it frees. A carrier outside the tree has no process mode to read,
			# and is live by its registration alone.
			if entity.owner.is_inside_tree() and not entity.owner.can_process():
				continue
			var state := entity.state_binding
			if state:
				var record_tick := tick
				var engine := engines.get(entity) as PredictionCore._PredictionEngine
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
		return NetwSnapshot.from_dictionary(_registry.sample(entity, tick))


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
			var snap := _registry.sample(entity, tick)
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
