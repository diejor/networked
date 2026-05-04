## Transforms raw crash manifest data for display in the debugger's Crash Panel.
@tool
class_name CrashAdapter
extends PanelDataAdapter

var alias_map: Dictionary = {}

func _init(p_tree_name: String, p_alias_map: Dictionary) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.CRASH
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.CRASH]]
	alias_map = p_alias_map

func feed(d: Dictionary) -> void:
	var entry: Dictionary = ManifestFormatter.format(d, alias_map)
	_push(entry)

func on_peer_event(_d: Dictionary, _connected: bool) -> void:
	pass

func get_current_label() -> String:
	return "%d crash%s" % [ring_buffer.size(), "es" if ring_buffer.size() != 1 else ""]

func get_status_banner_text() -> String:
	if ring_buffer.is_empty():
		return "No crashes detected."
	var last: Dictionary = ring_buffer[-1] as Dictionary
	return "Last: %s (Frame %d)" % [last.get("trigger", "UNKNOWN"), last.get("frame", 0)]

func get_status_level() -> int:
	return 2 if not ring_buffer.is_empty() else 0
