## Adapter that buffers [code]networked:clock_sample[/code] messages for one peer.
##
## [member ring_buffer] entries are raw pong dictionaries as emitted by
## [NetworkClock.pong_received]: rtt_raw, rtt_avg, rtt_jitter, diff, tick,
## display_offset, recommended_display_offset, is_stable, is_synchronized.
@tool
class_name ClockAdapter
extends PanelDataAdapter


func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.CLOCK
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.CLOCK]]


## Appends the clock sample dictionary and emits [signal PanelDataAdapter.data_changed].
func feed(d: Dictionary) -> void:
	_push(d)


## Returns the latest averaged RTT in milliseconds, e.g. [code]"12.3 ms"[/code].
func get_current_label() -> String:
	if ring_buffer.is_empty():
		return ""
	var last: Dictionary = ring_buffer[-1] as Dictionary
	return "%.1f ms" % ((last.get("rtt_avg", 0.0) as float) * 1000.0)
