## Adapter for the Topology panel.
##
## Ring buffer entries are raw [method NetTopologySnapshot.to_dict] output.
## The panel always shows the latest entry — topology is current-state, not a
## time-series — but the buffer is kept so reopening the panel restores the
## last known state without requiring a new snapshot from the game.
@tool
class_name TopologyAdapter
extends PanelDataAdapter


func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelDataAdapter.PanelType.TOPOLOGY
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[panel_type]]


func feed(d: Dictionary) -> void:
	_push(d)


func get_current_label() -> String:
	if ring_buffer.is_empty():
		return ""
	return "%d sync(s)" % (ring_buffer[-1].get("synchronizers", []) as Array).size()
