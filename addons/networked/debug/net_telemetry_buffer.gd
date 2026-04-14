## Game-side ring buffer for pre-failure telemetry.
##
## Records one entry per reporter flush cycle. When a failure is detected
## (race condition, C++ error), [method freeze] stops the write pointer so
## [method snapshot] can return the historical slice leading to the crash.
class_name NetTelemetryBuffer
extends RefCounted

var _capacity: int
var _entries: Array  # circular, length <= _capacity
var _write: int = 0  # next write index
var _count: int = 0  # entries actually stored
var _frozen: bool = false


func _init(capacity: int = 120) -> void:
	_capacity = capacity
	_entries.resize(capacity)


## Stop advancing the write pointer. Call this the moment a failure is detected.
func freeze() -> void:
	_frozen = true


## Resume recording (e.g. after a manifest has been shipped).
func thaw() -> void:
	_frozen = false


## Record one flush-cycle snapshot. Ignored while frozen.
func record(frame: int, cid_stack: Array, component_events: Array,
		peer_events: Array, lobby_snapshots: Dictionary) -> void:
	if _frozen:
		return
	_entries[_write] = {
		"frame": frame,
		"cid_stack": cid_stack.duplicate(),
		"component_events": component_events.duplicate(),
		"peer_events": peer_events.duplicate(),
		"lobby_snapshots": lobby_snapshots.duplicate(true),
	}
	_write = (_write + 1) % _capacity
	if _count < _capacity:
		_count += 1


## Return up to [param n] most-recent entries in chronological order.
## If [param n] is -1, returns all stored entries.
func snapshot(n: int = -1) -> Array:
	if _count == 0:
		return []
	var take := _count if n < 0 else mini(n, _count)
	var result: Array = []
	result.resize(take)
	# Oldest entry in the ring:
	var start: int = (_write - _count + _capacity) % _capacity
	for i in take:
		var offset := (_count - take + i)
		result[i] = _entries[(start + offset) % _capacity]
	return result


func clear() -> void:
	_write = 0
	_count = 0
	_frozen = false
