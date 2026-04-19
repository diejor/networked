@tool
class_name TopologyAdapter
extends PanelDataAdapter

func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelDataAdapter.PanelType.TOPOLOGY
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[panel_type]]

func feed(d: Dictionary) -> void:
	_push(d)

func on_peer_event(_d: Dictionary, _connected: bool) -> void:
	if not _connected:
		clear()

func get_current_label() -> String:
	if ring_buffer.is_empty():
		return ""
	return "%d sync(s)" % (ring_buffer[-1].get("synchronizers", []) as Array).size()
