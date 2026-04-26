## Base class for all per-peer panel data adapters.
##
## Each adapter owns a ring buffer for one peer+panel combination. Subclasses
## override [method feed] to append entries and emit [signal data_changed].
@tool
class_name PanelDataAdapter
extends RefCounted

signal data_changed(key: String)

enum PanelType { SPAN = 0, CRASH = 1, TOPOLOGY = 2 }

const PANEL_NAMES: Dictionary = {
	PanelType.SPAN:     "span",
	PanelType.CRASH:    "crash",
	PanelType.TOPOLOGY: "topology",
}

const PANEL_DISPLAY_NAMES: Dictionary = {
	PanelType.SPAN:     "Span Tracer",
	PanelType.CRASH:    "Crash Manifest",
	PanelType.TOPOLOGY: "Topology",
}

var adapter_key: String = ""
var tree_name: String = ""
var panel_type: PanelType = PanelType.SPAN

var ring_buffer: Array = []
var buffer_size: int = 512


func feed(_d: Dictionary) -> void:
	pass

func on_peer_event(_d: Dictionary, _connected: bool) -> void:
	pass

func on_span_event(_d: Dictionary, _type: String) -> void:
	pass

func get_current_label() -> String:
	return ""

## Returns a detailed single-line summary for the "Status Banner".
func get_status_banner_text() -> String:
	return ""

## Returns the severity level: 0=Info, 1=Warning, 2=Error.
## Used to color the status banner.
func get_status_level() -> int:
	return 0

func clear() -> void:
	ring_buffer.clear()
	data_changed.emit(adapter_key)

func populate(data: Array) -> void:
	ring_buffer = data.duplicate()
	if ring_buffer.size() > buffer_size:
		ring_buffer = ring_buffer.slice(-buffer_size)
	data_changed.emit(adapter_key)

func _push(entry: Variant) -> void:
	ring_buffer.append(entry)
	if ring_buffer.size() > buffer_size:
		ring_buffer.pop_front()
	data_changed.emit(adapter_key)
