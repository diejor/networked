@tool
class_name SpanAdapter
extends PanelDataAdapter

func _init(p_tree_name: String) -> void:
	tree_name = p_tree_name
	panel_type = PanelType.SPAN
	adapter_key = "%s:%s" % [tree_name, PANEL_NAMES[PanelType.SPAN]]

func on_span_event(d: Dictionary, type: String) -> void:
	_push({"type": type, "data": d})

func feed(d: Dictionary) -> void:
	on_span_event(d, "step")

func on_peer_event(_d: Dictionary, _connected: bool) -> void:
	if not _connected:
		clear()

func get_current_label() -> String:
	var open_count: int = 0
	for e: Dictionary in ring_buffer:
		if (e as Dictionary).get("type", "") == "open":
			open_count += 1
	return "%d spans" % open_count

func get_status_banner_text() -> String:
	if ring_buffer.is_empty():
		return "No spans traced."
	var open_count: int = 0
	var last_label: String = ""
	for e: Dictionary in ring_buffer:
		var type: String = (e as Dictionary).get("type", "")
		if type == "open":
			open_count += 1
			last_label = (e as Dictionary).get("data", {}).get("label", "")
		elif type == "close" or type == "fail":
			open_count -= 1
	
	if open_count > 0:
		return "Active: %d | Last: '%s'" % [open_count, last_label]
	return "All spans closed | Last: '%s'" % last_label

func get_status_level() -> int:
	for e: Dictionary in ring_buffer:
		if (e as Dictionary).get("type", "") == "fail":
			return 2
	return 0
