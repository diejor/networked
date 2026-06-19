## Public lag-compensation facade for one [MultiplayerTree].
##
## Exposed at [member NetwContext.lag_compensation], this is the query and action
## surface over server-recorded history. The server records authoritative state
## every tick keyed by the consumed input tick rather than the server clock, so a
## read at a logical tick returns the state that tick's input actually produced.
## That keying is the invariant every read here depends on. [method sample] and
## [method rewind] answer "where was an entity when the shooter saw it", and
## [method action] correlates an optimistic local effect with the authoritative
## result. See [PredictionComponent] for the client prediction and reconciliation
## side of the same loop.
##
## [codeblock]
## var lag := Netw.ctx(self).lag_compensation
## var past := lag.sample(target_entity, view_tick)   # detached NetwSnapshot
## if past.has_value(&"position"):
##     validate_hit(origin, dir, past.position)
##
## var action := lag.action(_place_bomb)
## action.predict = _predict_bomb
## action.request(view_tick)
## [/codeblock]
##
## [b]The input to state lifecycle[/b]
## [br]The owning client authors input every tick and predicts immediately, then
## ships that input toward the server stamped with its
## [member StampedSynchronizer.authored_tick]. The server consumes one input per
## tick through [PredictionComponent], so its consume frontier trails real time by
## about a half round trip. It records the produced state keyed by the consumed
## input tick. A read therefore names a logical tick of input, not a wall-clock
## moment.
## [codeblock]
## client : author input[t] -> predict state[t] -> record_input(t) / record_state(t)
##              |
##              |  ship input[t]   (StampedSynchronizer.authored_tick = t)
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
## [method MultiplayerInterpolator.displayed_authoring_tick], the server-authored
## tick it displayed when it acted.
##
## [br][br][b]Action readiness[/b]
## [br]An action evaluated at a view tick agrees with the client only when two
## independent conditions hold.
## [codeblock]
## 1. availability : the consume frontier reached view_tick, so
##                   PredictionComponent.has_consumed_state_tick(view_tick) holds
##                   and NetwTimeline.state_at(view_tick) is an exact slot
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
## [br]- [b]None.[/b] With no [LagCompensationService] mounted this facade no-ops,
## so a client-authoritative game carries nothing.
## [br]- [method sample] and [method rewind] assume a shared logical tick, server
## per-tick recording, bounded retention, and that only the owning client predicts.
## [br]- [method action] with [constant NetwAction.TimingMode.IMMEDIATE] adds
## discrete-event and propose-and-validate semantics.
## [br]- [constant NetwAction.TimingMode.TICK_ALIGNED_STATE_READY] adds the narrow
## assumptions that the action is owner-anchored, that the owner simulation is
## deterministic, and that resolution may be deferred behind the optimistic effect.
##
## [br][br]Resolves the backing [LagCompensationService] lazily, like
## [NetwInterest], so it degrades to safe empty results when no service is mounted.
class_name NetwLagCompensation
extends RefCounted

var _tree_ref: WeakRef

## Keyed optimistic effects owned by [LagCompensationService].
var effects: NetwEffects:
	get:
		var service := _service()
		return service.effects if service else NetwEffects.new()


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot], the analytic hit-validation read. See [method RewindQueries.sample].
##
## Returns an empty [NetwSnapshot] when no service is mounted, off the server, or
## for an entity with no retained history at [param tick].
##
## [codeblock]
## var past := Netw.ctx(self).lag_compensation.sample(target, view_tick)
## if past.has_value(&"position") and hits(origin, dir, past.position):
##     apply_damage(target)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	var service := _service()
	return service.sample(entity, tick) if service else NetwSnapshot.new()


## Applies each entity's state at [param tick] to its live node for the duration of
## [param body], then restores it. The opt-in heavyweight alternative to
## [method sample] for validation that needs real physics queries. See
## [method RewindQueries.rewind]. A no-op when no service is mounted.
##
## [codeblock]
## Netw.ctx(self).lag_compensation.rewind(targets, view_tick, func() -> void:
##     var hit := space.intersect_ray(query)   # targets at the perceived tick
##     ... )
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
	var service := _service()
	if service:
		service.rewind(entities, tick, body)


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
func timeline_of(entity: NetwEntity) -> NetwTimeline:
	var service := _service()
	return service.timeline_of(entity) if service else null


## Returns a [NetwAction] bound to [param authority].
##
## [param authority] must be a method [Callable] on the entity root or one of
## its children. The returned action uses [member effects] for local prediction
## and the mounted [LagCompensationService] for private request transport.
func action(authority: Callable) -> NetwAction:
	var service := _service()
	var slot := service._assign_action_slot(authority) if service else 0
	return NetwAction.new(self, authority, slot)


func _service() -> LagCompensationService:
	var mt := _tree()
	if not mt:
		return null
	var service := mt.get_service(LagCompensationService) as LagCompensationService
	if service:
		return service
	return mt.find_service_node(LagCompensationService) as LagCompensationService


func _tree() -> MultiplayerTree:
	return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null
