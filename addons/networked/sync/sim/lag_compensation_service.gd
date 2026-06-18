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

var _clock: MultiplayerClock
var _bind_attempts: int = 0
var _registry: TimelineRegistry = TimelineRegistry.new()
var _recorder: HistoryRecorder = HistoryRecorder.new()
var _runner: SimulationRunner = SimulationRunner.new()
var _queries: RewindQueries


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


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
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
	# The server holds the truth, so only it records authoritative history.
	if multiplayer and multiplayer.is_server():
		_recorder.record(_registry, tick)
