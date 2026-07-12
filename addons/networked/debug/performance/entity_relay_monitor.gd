@tool
## Exposes [NetwMultiplayer] carrier metrics as Godot [Performance] monitors, one group per
## [MultiplayerTree].
##
## This is a presentation adapter managed by the [DebugReporter]. It pulls
## [method NetwMultiplayer.monitor_snapshot] and turns cumulative counters into
## live rates, so monitoring overhead stays out of the core simulation loop.
## Work runs only while a debugger is attached and is throttled.
##
## [codeblock]
## Netw Relay <tree>/
##   ┠╴ drops_unknown_route_rate
##   ┠╴ drops_not_live_rate
##   ┠╴ drops_no_node_rate
##   ┖╴ derived_sets_active
## [/codeblock]
class_name EntityRelayMonitor
extends Node

const _SAMPLE_INTERVAL := 0.25

const _RATE_KEYS: Array[StringName] = [
	&"drops_unknown_route",
	&"drops_not_live",
	&"drops_no_node",
]

var _trees: Array[MultiplayerTree] = []
var _registered_ids: Dictionary[StringName, bool] = {}
var _latest_data: Dictionary[String, Dictionary] = {}
var _prev: Dictionary[String, Dictionary] = {}
var _accum: float = 0.0


func _process(delta: float) -> void:
	if Engine.is_editor_hint() or not EngineDebugger.is_active():
		return
	_accum += delta
	if _accum < _SAMPLE_INTERVAL:
		return
	_sample(_accum)
	_accum = 0.0


func _exit_tree() -> void:
	clear_all()


## Tracks [param mt] so its [NetwMultiplayer] carrier is sampled each interval.
func register_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		_trees.append(mt)


## Stops tracking [param mt] and removes its monitors.
func unregister_tree(mt: MultiplayerTree) -> void:
	_trees.erase(mt)
	if is_instance_valid(mt):
		_drop_category(_category(mt))


## Removes every registered monitor.
func clear_all() -> void:
	for id in _registered_ids.keys():
		if Performance.has_custom_monitor(id):
			Performance.remove_custom_monitor(id)
	_registered_ids.clear()
	_latest_data.clear()
	_prev.clear()


func _sample(elapsed: float) -> void:
	for mt in _trees:
		if not is_instance_valid(mt):
			continue
		if not mt.api:
			continue
		_sample_tree(_category(mt), mt.api.monitor_snapshot(), elapsed)


func _sample_tree(
		category: String,
		metrics: Dictionary,
		elapsed: float,
) -> void:
	var prev: Dictionary = _prev.get(category, {})
	var rates: Dictionary = {}
	for key in _RATE_KEYS:
		var cur := int(metrics.get(key, 0))
		rates[key] = float(cur - int(prev.get(key, cur))) \
				/ maxf(elapsed, 0.0001)
		prev[key] = cur
	_prev[category] = prev

	_latest_data[category] = {
		&"drops_unknown_route_rate": rates[&"drops_unknown_route"],
		&"drops_not_live_rate": rates[&"drops_not_live"],
		&"drops_no_node_rate": rates[&"drops_no_node"],
		&"derived_sets_active": metrics.get(&"derived_sets_active", 0),
	}
	_ensure_registered(category)


func _ensure_registered(category: String) -> void:
	var store: Dictionary = _latest_data[category]
	for key in store.keys():
		var id := StringName("%s/%s" % [category, key])
		if _registered_ids.has(id):
			continue
		var metric_key: StringName = key
		_reg(id, func() -> Variant:
			return _latest_data.get(category, {}).get(metric_key, 0)
		)


func _reg(id: StringName, callable: Callable) -> void:
	if _registered_ids.has(id):
		return
	if not Performance.has_custom_monitor(id):
		Performance.add_custom_monitor(
			id,
			callable,
			[],
			Performance.MONITOR_TYPE_QUANTITY,
		)
	_registered_ids[id] = true


func _drop_category(category: String) -> void:
	var prefix := category + "/"
	for id in _registered_ids.keys():
		if String(id).begins_with(prefix):
			if Performance.has_custom_monitor(id):
				Performance.remove_custom_monitor(id)
			_registered_ids.erase(id)
	_latest_data.erase(category)
	_prev.erase(category)


func _category(mt: MultiplayerTree) -> String:
	return "Netw Relay %s" % mt.get_tree_name()
