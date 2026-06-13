## Throwaway per-entity tick-keyed timeline for the lag-compensation spike.
##
## Models the future [code]NetwTimeline[/code] (architecture P1) closely enough
## to falsify its core invariants: whole-entity snapshot granularity, the
## carry-forward asymmetry (state carries, input does not), and the ack-trim
## watermark. Deleted when Phase 0 lands.
##
## Spike finding baked in: the project [HistoryBuffer] has neither
## [code]trim_before[/code] nor a carry-forward read, so this builds both on top
## of its ring + [method HistoryBuffer.bracketing_ticks]. The ring evicts old
## entries physically by capacity; [method trim_before] is a thin logical floor
## on top, which is the real shape Phase 0 will need.
class_name SpikeTimeline
extends RefCounted

const NEUTRAL: Dictionary = {}

var _state: HistoryBuffer
var _input: HistoryBuffer
var _floor_tick: int = -1


func _init(capacity: int = 256) -> void:
	_state = HistoryBuffer.new(capacity)
	_input = HistoryBuffer.new(capacity)


## Records a whole-entity state [param snapshot] at [param tick].
func record_state(tick: int, snapshot: Dictionary) -> void:
	if tick <= _floor_tick:
		return
	_state.record(tick, snapshot.duplicate(true))


## Records a whole-entity input [param snapshot] at [param tick].
func record_input(tick: int, snapshot: Dictionary) -> void:
	if tick <= _floor_tick:
		return
	_input.record(tick, snapshot.duplicate(true))


## Returns the exact state recorded at [param tick], or [constant NEUTRAL].
func state_at(tick: int) -> Dictionary:
	var v: Variant = _state.get_at(tick)
	return v if v != null else NEUTRAL


## Returns the latest state at or before [param tick] (carry-forward).
func latest_state_at_or_before(tick: int) -> Dictionary:
	var prev: int = _state.bracketing_ticks(tick).x
	if prev < 0 or prev <= _floor_tick:
		return NEUTRAL
	var v: Variant = _state.get_at(prev)
	return v if v != null else NEUTRAL


## Returns [code]true[/code] if a state snapshot exists exactly at [param tick].
func has_state_at(tick: int) -> bool:
	return tick > _floor_tick and _state.get_at(tick) != null


## Returns the exact input at [param tick]. Input never carries forward.
func input_at(tick: int) -> Dictionary:
	var v: Variant = _input.get_at(tick)
	return v if v != null else NEUTRAL


## Returns [code]true[/code] if an input snapshot exists exactly at [param tick].
func has_input_at(tick: int) -> bool:
	return tick > _floor_tick and _input.get_at(tick) != null


## Returns recorded inputs in the inclusive range, in tick order.
##
## Each entry is [code]{ "tick": int, "input": Dictionary }[/code]. This is the
## replay window reconciliation walks.
func inputs_in_range(from_tick: int, to_tick: int) -> Array:
	var out: Array = []
	for t in range(from_tick, to_tick + 1):
		var v: Variant = _input.get_at(t)
		if v != null:
			out.append({"tick": t, "input": v})
	return out


## Raises the GC floor to [param tick]; reads at or before it return neutral.
func trim_before(tick: int) -> void:
	_floor_tick = maxi(_floor_tick, tick)


## Returns the newest recorded state tick, or [code]-1[/code].
func newest_state_tick() -> int:
	return _state.newest_tick()


## Returns the newest recorded input tick, or [code]-1[/code].
func newest_input_tick() -> int:
	return _input.newest_tick()


## Returns the oldest readable state tick above the trim floor.
func oldest_state_tick() -> int:
	var oldest: int = _state.oldest_tick()
	return oldest if oldest > _floor_tick else _floor_tick + 1


## Returns the current trim floor tick.
func floor_tick() -> int:
	return _floor_tick
