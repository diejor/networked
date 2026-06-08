## Pre-allocated ring buffer that stores values keyed by tick number.
##
## Uses power-of-two capacity for fast bitwise indexing and optimized 
## searching to minimize GDScript overhead in hot loops.
class_name HistoryBuffer
extends RefCounted

var _slots: Array
var _ticks: PackedInt32Array
var _head: int = 0
var _count: int = 0
var _capacity: int
var _mask: int


func _init(capacity: int = 16) -> void:
	# Force power-of-two for bitwise optimization
	_capacity = 1
	while _capacity < capacity:
		_capacity <<= 1

	_mask = _capacity - 1
	_slots.resize(_capacity)
	_ticks.resize(_capacity)
	_ticks.fill(-1)


## Records a value at the given tick.
func record(tick: int, value: Variant) -> void:
	var idx: int
	if _count < _capacity:
		idx = (_head + _count) & _mask
		_count += 1
	else:
		idx = _head
		_head = (_head + 1) & _mask

	_slots[idx] = value
	_ticks[idx] = tick


## Returns the value at exactly [param tick].
func get_at(tick: int) -> Variant:
	for i in _count:
		var idx := (_head + i) & _mask
		if _ticks[idx] == tick:
			return _slots[idx]
	return null


## Returns the [code](prev, next)[/code] ticks bracketing [param tick].
##
## [code]x[/code] is the greatest recorded tick less than or equal to
## [param tick] or [code]-1[/code] if none exists. [code]y[/code] is the
## smallest recorded tick strictly greater than [param tick] or
## [code]-1[/code] if none exists.
func bracketing_ticks(tick: int) -> Vector2i:
	var prev_tick := -1
	var next_tick := -1
	for i in _count:
		var idx := (_head + i) & _mask
		var t := _ticks[idx]
		if t <= tick:
			if t > prev_tick:
				prev_tick = t
		else:
			if next_tick == -1 or t < next_tick:
				next_tick = t
				break
	return Vector2i(prev_tick, next_tick)


# perf: no-alloc variant for the interpolation hot path.
func find_bracketing_ticks(tick: int, _hint_tick: int, out_pair: PackedInt32Array) -> void:
	var pair := bracketing_ticks(tick)
	out_pair[0] = pair.x
	out_pair[1] = pair.y


## Returns [code]true[/code] if there is at least one entry recorded strictly after [param tick].
func has_tick_after(tick: int) -> bool:
	# Optimization: Since ticks are chronological, we only need to check the newest one.
	return newest_tick() > tick


func oldest_tick() -> int:
	return _ticks[_head] if _count > 0 else -1


func newest_tick() -> int:
	return _ticks[(_head + _count - 1) & _mask] if _count > 0 else -1


func clear() -> void:
	_count = 0
	_head = 0


func size() -> int:
	return _count


func is_empty() -> bool:
	return _count == 0
