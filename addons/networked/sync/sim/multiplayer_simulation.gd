@tool
## Session service that drives every entity's per-tick simulation in one
## deterministic order.
##
## A single subscriber to [signal MultiplayerClock.on_tick] steps each registered
## [PredictionComponent] in a stable order so the server consumes input for every
## entity the same way every run. Reconciliation on the owning client is event
## driven off [member StateSynchronizer.on_state_received], not a tick step, so
## this service never touches it. Capability logic stays in the component, this
## owns only ordering and metrics.
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
