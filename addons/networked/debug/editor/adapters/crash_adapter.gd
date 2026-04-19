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
