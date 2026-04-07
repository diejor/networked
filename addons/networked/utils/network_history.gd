## Stores per-node state and input history for rollback.
##
## Used by [code]RollbackManager[/code] (Phase 2). Records both authoritative state snapshots
## (from [code]RollbackSynchronizer[/code]) and input snapshots (from [code]NetworkInputSynchronizer[/code])
## so the rollback loop can restore any past tick and replay from there.
class_name NetworkHistory
extends RefCounted

## How many ticks of history to retain before trimming.
var history_limit: int

# node → {StringName → HistoryBuffer}
var _state_buffers: Dictionary = {}
var _input_buffers: Dictionary = {}


func _init(limit: int) -> void:
	history_limit = limit


## Records a state [param value] for [param node].[param property] at [param tick].
func record(node: Node, property: StringName, tick: int, value: Variant) -> void:
	_get_or_create(_state_buffers, node, property).record(tick, value)


## Records an input [param value] for [param node].[param property] at [param tick].
func record_input(node: Node, property: StringName, tick: int, value: Variant) -> void:
	_get_or_create(_input_buffers, node, property).record(tick, value)


## Restores [param node].[param property] to the latest known value at or before [param tick].
func restore(node: Node, property: StringName, tick: int) -> void:
	var buf := _get_buffer(_state_buffers, node, property)
	if not buf:
		return
	var val: Variant = buf.get_latest_at_or_before(tick)
	if val != null:
		node.set_indexed(NodePath(property), val)


## Returns the input value for [param node].[param property] at or before [param tick].
func get_input(node: Node, property: StringName, tick: int) -> Variant:
	var buf := _get_buffer(_input_buffers, node, property)
	return buf.get_latest_at_or_before(tick) if buf else null


## Returns [code]true[/code] if any input was recorded for [param node] at exactly [param tick].
func has_input(node: Node, tick: int) -> bool:
	if not _input_buffers.has(node):
		return false
	for prop: StringName in _input_buffers[node]:
		var buf: HistoryBuffer = _input_buffers[node][prop]
		if buf.get_at(tick) != null:
			return true
	return false


## Evicts all entries older than [param tick] from all buffers.
func trim_before(tick: int) -> void:
	for store in [_state_buffers, _input_buffers]:
		for node in store:
			for prop in store[node]:
				(store[node][prop] as HistoryBuffer).trim_before(tick)


func _get_or_create(store: Dictionary, node: Node, prop: StringName) -> HistoryBuffer:
	if not store.has(node):
		store[node] = {}
	if not store[node].has(prop):
		store[node][prop] = HistoryBuffer.new(history_limit)
	return store[node][prop]


func _get_buffer(store: Dictionary, node: Node, prop: StringName) -> HistoryBuffer:
	return store.get(node, {}).get(prop, null)
