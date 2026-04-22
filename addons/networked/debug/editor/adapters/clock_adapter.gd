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

func get_status_banner_text() -> String:
	if ring_buffer.is_empty():
		return "Waiting for clock data..."
	var last: Dictionary = ring_buffer[-1] as Dictionary
	var rtt := (last.get("rtt_avg", 0.0) as float) * 1000.0
	var jitter := (last.get("rtt_jitter", 0.0) as float) * 1000.0
	var stable: bool = last.get("is_stable", false)
	var synced: bool = last.get("is_synchronized", false)
	
	var status := "Stable" if stable else "UNSTABLE"
	if not synced: status = "Syncing..."
	
	return "RTT: %.1fms | Jitter: %.1fms | %s" % [rtt, jitter, status]

func get_status_level() -> int:
	if ring_buffer.is_empty(): return 0
	var last: Dictionary = ring_buffer[-1] as Dictionary
	if not last.get("is_stable", false): return 1
	if (last.get("rtt_avg", 0.0) as float) * 1000.0 > 100.0: return 1
	return 0
