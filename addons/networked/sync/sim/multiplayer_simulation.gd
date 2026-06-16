@tool
## Session service that drives every entity's per-tick simulation in one
## deterministic order.
##
## A single subscriber to [signal MultiplayerClock.on_tick] steps each registered
## [PredictionComponent] in a stable order so the server consumes input for every
## entity the same way every run. Reconciliation on the owning client is event
## driven off [member StateSynchronizer.on_state_received], not a tick step, so
## this service never touches it. Capability logic stays in the component, this
## owns only ordering, the per-entity timeline registry, and metrics.
##
## [br][br][b]Timeline registry[/b]
## [br]The server keeps one [NetwTimeline] per entity, keyed by [NetwEntity], as
## the rewind substrate. A [StateSynchronizer] registers its entity through
## [method register_timeline] when it spawns, so an entity is rewindable by
## default without a [PredictionComponent]. After each tick the server records
## every registered entity's authoritative [method StampedSynchronizer.snapshot_payload]
## into its timeline, so non-predicted state-synced entities have history too.
## [method timeline_of] is the query seam the future server rewind reads.
##
## [codeblock]
## MultiplayerClock.on_tick
##   └─ MultiplayerSimulation._on_tick(delta, tick)
##        for pc in components sorted by entity_id:
##            pc.simulate_tick(delta, tick)   # predict or consume, per role
## [/codeblock]
##
## Registered through [NetwServices] per [MultiplayerTree], like [MultiplayerClock],
## so several trees in one [SceneTree] each get their own simulation loop. Mount it
## as a sibling of the clock under the session root.
class_name MultiplayerSimulation
extends Node

var _clock: MultiplayerClock
var _components: Array[PredictionComponent] = []
# Server-side rewind substrate: one authoritative timeline per entity. Keyed by
# the RefCounted NetwEntity so there is no node-path coupling.
var _timelines: Dictionary[NetwEntity, NetwTimeline] = { }


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	var mt := NetwServices.register(self, MultiplayerSimulation)
	if not is_instance_valid(mt):
		return
	if not mt.session_entered.is_connected(_bind_clock):
		mt.session_entered.connect(_bind_clock)
	if mt.is_online():
		_bind_clock.call_deferred()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	_unbind_clock()
	NetwServices.unregister(self, MultiplayerSimulation)


## Registers [param pc] so it is stepped each tick. Idempotent.
##
## The owning client's predictor and the server's consumer register here; remote
## displays do not, so the loop only ever steps entities this peer simulates.
func register(pc: PredictionComponent) -> void:
	if pc not in _components:
		_components.append(pc)


## Removes [param pc] from the simulation loop.
func unregister(pc: PredictionComponent) -> void:
	_components.erase(pc)


## Registers [param entity] for server-side authoritative recording, returning its
## [NetwTimeline]. Idempotent: a repeat call returns the existing timeline.
##
## A [StateSynchronizer] calls this when it spawns, and the server [PredictionComponent]
## roles read the same timeline back, so the trigger is state-sync presence, not
## prediction. The created timeline is published to [member NetwEntity.timeline].
##
## [br][br][b]Server Only.[/b]
func register_timeline(entity: NetwEntity) -> NetwTimeline:
	if not entity:
		return null
	var existing := _timelines.get(entity) as NetwTimeline
	if existing:
		return existing
	var tl := NetwTimeline.new()
	_timelines[entity] = tl
	entity.timeline = tl
	return tl


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
##
## This is the enumeration seam the server rewind query reads.
func timeline_of(entity: NetwEntity) -> NetwTimeline:
	return _timelines.get(entity) as NetwTimeline


## Drops [param entity]'s timeline from the registry.
func unregister_timeline(entity: NetwEntity) -> void:
	_timelines.erase(entity)


## Returns aggregate counters for the debug overlay.
##
## [codeblock]
## {
##   ┠╴ entities: int      # registered components stepped this tick
##   ┠╴ corrections: int   # summed reconciliation snaps since spawn
##   ┖╴ max_replay_depth: int  # worst replay window walked
## }
## [/codeblock]
func metrics() -> Dictionary:
	var corrections := 0
	var max_replay := 0
	for pc in _components:
		if not is_instance_valid(pc):
			continue
		corrections += pc.corrections
		max_replay = maxi(max_replay, pc.max_replay_depth)
	return {
		&"entities": _components.size(),
		&"corrections": corrections,
		&"max_replay_depth": max_replay,
	}


func _bind_clock() -> void:
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return
	var clock := mt.get_service(MultiplayerClock) as MultiplayerClock
	if not clock:
		return
	_clock = clock
	if not clock.on_tick.is_connected(_on_tick):
		clock.on_tick.connect(_on_tick)


func _unbind_clock() -> void:
	if is_instance_valid(_clock) and _clock.on_tick.is_connected(_on_tick):
		_clock.on_tick.disconnect(_on_tick)
	_clock = null


func _on_tick(delta: float, tick: int) -> void:
	for pc in _ordered():
		if is_instance_valid(pc):
			pc.simulate_tick(delta, tick)
	_record_authoritative_state(tick)


# Records every registered entity's authoritative snapshot after the tick's
# simulation, so non-predicted state-synced entities have rewind history too.
# Runs only on the server, where the StateSynchronizer holds the truth.
func _record_authoritative_state(tick: int) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	for entity in _timelines:
		if not is_instance_valid(entity.owner):
			continue
		var state := entity.state
		if state:
			_timelines[entity].record_state(tick, state.snapshot_payload())


# Stable order by entity id so the server consumes every entity identically each
# run. The set is small, so a per-tick sort of a copy is cheap and keeps the
# registry insertion-order free.
func _ordered() -> Array[PredictionComponent]:
	var out := _components.duplicate()
	out.sort_custom(
		func(a: PredictionComponent, b: PredictionComponent) -> bool:
			return a.order_key() < b.order_key()
	)
	return out
