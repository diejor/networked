## Pre-allocated ring buffer that stores values keyed by tick number.
##
## Uses parallel arrays (_slots and _ticks) with head/tail pointer arithmetic for O(1)
## insert and eviction. Designed for use by TickInterpolator, RollbackSynchronizer, and
## NetworkInputSynchronizer.
class_name HistoryBuffer
extends RefCounted

var _slots: Array
var _ticks: PackedInt32Array
var _head: int = 0
var _count: int = 0
var _capacity: int


func _init(capacity: int) -> void:
	_capacity = capacity
	_slots.resize(capacity)
	_ticks.resize(capacity)
	_ticks.fill(-1)


## Records [param value] at [param tick], evicting the oldest entry when full.
func record(tick: int, value: Variant) -> void:
	var idx: int
	if _count < _capacity:
		idx = (_head + _count) % _capacity
		_count += 1
	else:
		idx = _head
		_head = (_head + 1) % _capacity
	_slots[idx] = value
	_ticks[idx] = tick


## Returns the value recorded at exactly [param tick], or [code]null[/code] if not found.
func get_at(tick: int) -> Variant:
	for i in _count:
		var idx := (_head + i) % _capacity
		if _ticks[idx] == tick:
			return _slots[idx]
	return null


## Returns the value at the highest tick <= [param tick], or [code]null[/code] if none exists.
func get_latest_at_or_before(tick: int) -> Variant:
	var best_tick := -1
	var best_value: Variant = null
	for i in _count:
		var idx := (_head + i) % _capacity
		var t := _ticks[idx]
		if t <= tick and t > best_tick:
			best_tick = t
			best_value = _slots[idx]
	return best_value


## Returns the tick of the highest entry <= [param tick], or [code]-1[/code] if none exists.
func get_latest_tick_at_or_before(tick: int) -> int:
	var best_tick := -1
	for i in _count:
		var idx := (_head + i) % _capacity
		var t := _ticks[idx]
		if t <= tick and t > best_tick:
			best_tick = t
	return best_tick


## Returns the tick of the earliest entry strictly after [param tick], or [code]-1[/code] if none exists.
func get_earliest_tick_after(tick: int) -> int:
	var best_tick := -1
	for i in _count:
		var idx := (_head + i) % _capacity
		var t := _ticks[idx]
		if t > tick and (best_tick == -1 or t < best_tick):
			best_tick = t
	return best_tick


## Returns the tick of the oldest stored entry, or [code]-1[/code] if the buffer is empty.
func oldest_tick() -> int:
	if _count == 0:
		return -1
	return _ticks[_head]


## Returns the tick of the newest stored entry, or [code]-1[/code] if the buffer is empty.
func newest_tick() -> int:
	if _count == 0:
		return -1
	return _ticks[(_head + _count - 1) % _capacity]


## Removes all entries with a tick strictly less than [param tick].
func trim_before(tick: int) -> void:
	while _count > 0 and _ticks[_head] < tick:
		_ticks[_head] = -1
		_slots[_head] = null
		_head = (_head + 1) % _capacity
		_count -= 1


## Returns [code]true[/code] if the buffer contains no entries.
func is_empty() -> bool:
	return _count == 0
