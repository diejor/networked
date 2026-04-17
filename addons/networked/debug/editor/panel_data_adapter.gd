## Base class for all per-peer panel data adapters.
##
## Each adapter owns a ring buffer for one peer+panel combination. Subclasses
## override [method feed] (or [method SpanAdapter.feed_span]) to append entries
## and emit [signal data_changed]. The UI calls [method clear] and reads
## [member ring_buffer] directly; it never pulls data, all updates are pushed.
@tool
class_name PanelDataAdapter
extends RefCounted

## Emitted after a new entry is appended to [member ring_buffer].
## [param key] matches [member adapter_key] so listeners can route without captures.
signal data_changed(key: String)

## Canonical panel type identifiers. Define once, reference everywhere.
enum PanelType { CLOCK = 0, SPAN = 1, CRASH = 2 }

## String names used in adapter keys: [code]"%s:%s" % [tree_name, PANEL_NAMES[type]][/code]
const PANEL_NAMES: Dictionary = {
	PanelType.CLOCK: "clock",
	PanelType.SPAN:  "span",
	PanelType.CRASH: "crash",
}

## Display labels shown in the left-tree checkboxes and panel title bars.
const PANEL_DISPLAY_NAMES: Dictionary = {
	PanelType.CLOCK: "Clock",
	PanelType.SPAN:  "Span Tracer",
	PanelType.CRASH: "Crash Manifest",
}

## Stable key: [code]"%s:%s" % [tree_name, PANEL_NAMES[panel_type]][/code]
var adapter_key: String = ""
var tree_name: String = ""
var panel_type: PanelType = PanelType.CLOCK

## Newest entry at the back ([code]ring_buffer[-1][/code]).
var ring_buffer: Array = []
var buffer_size: int = 512


## Called by [DebuggerSession] when the peer's data source becomes available.
## Override to subscribe to signals or register callbacks.
func connect_to_source() -> void:
	pass


## Called by [DebuggerSession] when the peer disconnects or the session resets.
func disconnect_from_source() -> void:
	pass


## Override to append [param d] to [member ring_buffer] and emit [signal data_changed].
func feed(d: Dictionary) -> void:
	pass


## Override to return a live one-line metric shown in the panel title bar.
func get_current_label() -> String:
	return ""


## Clears [member ring_buffer] and emits [signal data_changed].
func clear() -> void:
	ring_buffer.clear()
	data_changed.emit(adapter_key)


## Appends [param entry] respecting [member buffer_size], then emits [signal data_changed].
## Shared by all subclass [method feed] implementations.
func _push(entry: Variant) -> void:
	ring_buffer.append(entry)
	if ring_buffer.size() > buffer_size:
		ring_buffer.pop_front()
	data_changed.emit(adapter_key)
