@tool
class_name ClockAdapter
extends PanelDataAdapter

func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.CLOCK
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.CLOCK]]

func feed(d: Dictionary) -> void:
	_push(d)

func on_peer_event(_d: Dictionary, _connected: bool) -> void:
	if not _connected:
		clear()

func get_current_label() -> String:
	if ring_buffer.is_empty():
		return ""
	var last: Dictionary = ring_buffer[-1] as Dictionary
	return "%.1f ms" % ((last.get("rtt_avg", 0.0) as float) * 1000.0)
