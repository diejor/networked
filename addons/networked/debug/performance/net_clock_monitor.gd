## Tracks and exposes [NetworkClock] metrics as Godot [Performance] monitors.
##
## This is managed by the [NetworkedDebugReporter] to ensure that monitoring
## overhead remains isolated from core networking logic. It handles both
## local clocks (via signals) and remote clocks (via debug-only relayed pongs).
@tool
class_name NetClockMonitor
extends Node

## Set of monitor IDs currently registered to avoid duplicates and ensure
## cleanup.
var _registered_ids: Dictionary[StringName, bool] = {}

## Persistent storage to ensure Callables always have a stable reference to 
## the latest data.
## [br][br]
## [b]Key:[/b] Category String (e.g. "Clock Admin")
## [br][br]
## [b]Value:[/b] Dictionary of latest metrics.
var _latest_data: Dictionary = {}


func _exit_tree() -> void:
	clear_all()


## Clears all registered monitors.
func clear_all() -> void:
	for id in _registered_ids.keys():
		if Performance.has_custom_monitor(id):
			Performance.remove_custom_monitor(id)
	_registered_ids.clear()
	_latest_data.clear()


## Updates metrics for a local tree's clock.
func update_local_clock(mt: MultiplayerTree, data: Dictionary) -> void:
	var category := _get_category(mt, 0, data)
	_update_data_and_register(category, data)


## Updates metrics for a remote peer's clock (relayed via Editor).
func update_relayed_clock(envelope: NetEnvelope) -> void:
	var peer_id := envelope.peer_id
	var data := envelope.payload
	var category := _get_category(null, peer_id, data)
	_update_data_and_register(category, data)


## Removes monitors for a relayed remote peer.
func remove_relayed_clock(envelope: NetEnvelope) -> void:
	var peer_id := envelope.peer_id
	var data := envelope.payload
	var category := _get_category(null, peer_id, data)
	
	_latest_data.erase(category)
	
	for key in ["rtt", "rtt_avg", "jitter", "error", "offset", "target"]:
		var id: StringName = category + "/" + key
		if Performance.has_custom_monitor(id):
			Performance.remove_custom_monitor(id)
		_registered_ids.erase(id)


func _get_category(mt: MultiplayerTree, _p_id: int, data: Dictionary) -> String:
	var username := data.get("username", "")
	if username.is_empty():
		if is_instance_valid(mt) and mt.local_player:
			var player := SpawnerPlayerComponent.unwrap(mt.local_player)
			if player:
				username = player.username
			else:
				username = mt.local_player.name.get_slice("|", 0)
		else:
			username = data.get("tree_name", "Unknown")

	return "Clock %s" % username


func _update_data_and_register(category: String, data: Dictionary) -> void:
	# Update persistent storage so existing callables see the new values.
	if not _latest_data.has(category):
		_latest_data[category] = data.duplicate()
	else:
		for k in data:
			_latest_data[category][k] = data[k]
	
	_ensure_monitors_registered(category)


func _ensure_monitors_registered(category: String) -> void:
	var prefix := category + "/"
	var store: Dictionary = _latest_data[category]
	
	# Time-based metrics (expect seconds, format as ms)
	_reg(prefix + "rtt", 
			func(): return store.get("rtt_raw", 0.0), 
			Performance.MONITOR_TYPE_TIME)
	_reg(prefix + "rtt_avg", 
			func(): return store.get("rtt_avg", 0.0), 
			Performance.MONITOR_TYPE_TIME)
	_reg(prefix + "jitter", 
			func(): return store.get("rtt_jitter", 0.0), 
			Performance.MONITOR_TYPE_TIME)
	
	# Quantity-based metrics (ticks)
	_reg(prefix + "error", 
			func(): return store.get("diff", 0), 
			Performance.MONITOR_TYPE_QUANTITY)
	_reg(prefix + "offset", 
			func(): return store.get("display_offset", 0), 
			Performance.MONITOR_TYPE_QUANTITY)
	_reg(prefix + "target", 
			func(): return store.get("recommended_display_offset", 0), 
			Performance.MONITOR_TYPE_QUANTITY)


func _reg(id: StringName, callable: Callable, type: int) -> void:
	if not _registered_ids.has(id):
		if not Performance.has_custom_monitor(id):
			Performance.add_custom_monitor(id, callable, [], type)
		_registered_ids[id] = true
