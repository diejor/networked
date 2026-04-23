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
	pass

func get_current_label() -> String:
	if ring_buffer.is_empty():
		return ""
	return "%d sync(s)" % (ring_buffer[-1].get("synchronizers", []) as Array).size()

func get_status_banner_text() -> String:
	if ring_buffer.is_empty():
		return "Waiting for topology snapshot..."
	var last: Dictionary = ring_buffer[-1] as Dictionary
	var syncs := (last.get("synchronizers", []) as Array).size()
	var cache_info: Dictionary = last.get("cache_info", {})
	var hooked: bool = cache_info.get("hooked", false)
	var hit: bool = cache_info.get("hit", false)
	
	var cache_str := "OK"
	if not hooked: cache_str = "NOT HOOKED"
	elif not hit: cache_str = "CACHE MISS"
	
	return "Syncs: %d | Cache: %s | Scene: %s" % [syncs, cache_str, last.get("active_scene", "None").get_file()]

func get_status_level() -> int:
	if ring_buffer.is_empty(): return 0
	var cache_info: Dictionary = ring_buffer[-1].get("cache_info", {})
	if not cache_info.get("hooked", false): return 2
	if not cache_info.get("hit", false): return 1
	return 0
