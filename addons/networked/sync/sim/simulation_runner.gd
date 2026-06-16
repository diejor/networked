## Steps every registered [PredictionComponent] each tick in one deterministic order.
##
## A stable order by [member NetwEntity.entity_id] means the server consumes every
## entity identically each run, so a replayed trace is reproducible. The owning
## client's predictor and the server's consumer register here; remote displays do
## not, so the loop only ever steps entities this peer simulates.
##
## [codeblock]
## # LagCompensationService._on_tick:
## runner.step(delta, tick)   # predict or consume, per role, in id order
## [/codeblock]
##
## Owned by [LagCompensationService]. Capability logic stays in the component, this
## owns only ordering and metric aggregation.
class_name SimulationRunner
extends RefCounted

var _components: Array[PredictionComponent] = []


## Registers [param pc] so it is stepped each tick. Idempotent.
func register(pc: PredictionComponent) -> void:
	if pc not in _components:
		_components.append(pc)


## Removes [param pc] from the step loop.
func unregister(pc: PredictionComponent) -> void:
	_components.erase(pc)


## Steps every registered component for [param tick] in deterministic order.
func step(delta: float, tick: int) -> void:
	for pc in _ordered():
		if is_instance_valid(pc):
			pc.simulate_tick(delta, tick)


## Returns aggregate counters for the debug overlay.
##
## [codeblock]
## {
##   ┠╴ entities: int          # registered components stepped this tick
##   ┠╴ corrections: int       # summed reconciliation snaps since spawn
##   ┠╴ max_replay_depth: int  # worst replay window walked
##   ┠╴ consumed: int          # summed inputs the server consumed
##   ┖╴ missing: int           # summed input ticks stepped over as lost
## }
## [/codeblock]
func metrics() -> Dictionary:
	var corrections := 0
	var max_replay := 0
	var consumed := 0
	var missing := 0
	for pc in _components:
		if not is_instance_valid(pc):
			continue
		corrections += pc.corrections
		max_replay = maxi(max_replay, pc.max_replay_depth)
		consumed += pc.consumed_count
		missing += pc.missing_count
	return {
		&"entities": _components.size(),
		&"corrections": corrections,
		&"max_replay_depth": max_replay,
		&"consumed": consumed,
		&"missing": missing,
	}


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
