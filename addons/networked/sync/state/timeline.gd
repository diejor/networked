## Per-entity, tick-keyed record of whole-entity state and input snapshots.
##
## State snapshots carry forward so a missing tick reads as unchanged, while
## input snapshots are exact so a missing tick reads as no action. Each side has
## exactly one writer (state: the server, input: the owning peer), so there is
## never a merge and no authority flags are needed.
##
## [codeblock]
## # Reconciliation on the owning client (architecture §4.3):
## var predicted := timeline.latest_state_at_or_before(ack + 1)
## if diverged(predicted, authoritative):
##     for entry in timeline.inputs_in_range(ack + 1, now):
##         _network_tick(entry.input, delta, entry.tick, false)
## timeline.trim_before(ack)
## [/codeblock]
##
## Snapshots are whole-entity [Dictionary] values keyed by a tick number, with
## each entry mapping a [ProxySynchronizer] virtual name to its value. Two
## [HistoryBuffer] rings back the store, one for state and one for input, so a
## restore is a single atomic [method state_at] read rather than a per-property
## walk.
class_name NetwTimeline
extends RefCounted

## Default ring capacity, roughly one second of ticks at 60 Hz. Reconciliation
## only ever replays an [member NetwTimeline.input] window of order RTT ticks, so
## this is sized for the server rewind retention window, not the replay depth.
const DEFAULT_LIMIT := 64

## Tick → [code]{virtual_name: value}[/code] authoritative state snapshots.
var state: HistoryBuffer

## Tick → [code]{virtual_name: value}[/code] input snapshots.
var input: HistoryBuffer

# Logical GC watermark. Entries recorded strictly before this tick are treated
# as absent by every read, so trimming needs no physical eviction (the ring
# self-bounds by capacity).
var _floor_tick: int = -1


func _init(limit: int = DEFAULT_LIMIT) -> void:
	state = HistoryBuffer.new(limit)
	input = HistoryBuffer.new(limit)


## Records an authoritative whole-entity state [param snapshot] at [param tick].
func record_state(tick: int, snapshot: Dictionary) -> void:
	state.record(tick, snapshot)


## Records an input [param snapshot] at [param tick].
func record_input(tick: int, snapshot: Dictionary) -> void:
	input.record(tick, snapshot)


## Returns the exact state snapshot at [param tick], or an empty [Dictionary].
func state_at(tick: int) -> Dictionary:
	return _exact(state, tick)


## Returns the newest state snapshot at or before [param tick] (carry-forward),
## or an empty [Dictionary] when nothing at or before it survives the watermark.
func latest_state_at_or_before(tick: int) -> Dictionary:
	if tick < _floor_tick:
		return { }
	var prev := state.bracketing_ticks(tick).x
	if prev < _floor_tick:
		return { }
	var value: Variant = state.get_at(prev)
	return value if value is Dictionary else { }


## Returns the exact input snapshot at [param tick], or an empty [Dictionary].
##
## Input never carries forward: a missing tick is a deliberate "no action", not a
## stale repeat. Use [method record_input]'s exact key, never a bracketed read.
func input_at(tick: int) -> Dictionary:
	return _exact(input, tick)


## Returns [code]true[/code] when an input snapshot is recorded at [param tick].
func has_input_at(tick: int) -> bool:
	return not input_at(tick).is_empty()


## Returns the newest recorded input tick, or [code]-1[/code] when empty.
##
## The server consume step reads this to tell a lost input tick (a later one has
## arrived) from one that simply has not arrived yet.
func newest_input_tick() -> int:
	return input.newest_tick()


## Returns recorded input snapshots for the inclusive tick range, tick-ascending.
##
## This is the reconciliation replay window: each element is
## [code]{tick: int, input: Dictionary}[/code] for the ticks that actually
## carry input, skipping gaps. The low bound is clamped to the trim watermark.
func inputs_in_range(from: int, to: int) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var start: int = maxi(from, _floor_tick)
	for tick in range(start, to + 1):
		var value: Variant = input.get_at(tick)
		if value is Dictionary:
			out.append({ &"tick": tick, &"input": value })
	return out


## Advances the GC watermark so entries before [param tick] read as absent.
##
## Memory is already bounded by the ring capacity, so this is a logical trim: it
## moves the floor monotonically and never rewinds it.
func trim_before(tick: int) -> void:
	_floor_tick = maxi(_floor_tick, tick)


func _exact(buffer: HistoryBuffer, tick: int) -> Dictionary:
	if tick < _floor_tick:
		return { }
	var value: Variant = buffer.get_at(tick)
	return value if value is Dictionary else { }
