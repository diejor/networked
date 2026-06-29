@tool
## Exposes [LagCompensation] health as Godot [Performance] monitors, one group per
## [MultiplayerTree].
##
## This is a presentation adapter managed by the [NetworkedDebugReporter]. It pulls
## [method LagCompensation.metrics] and turns the cumulative counters into live
## rates, so monitoring overhead stays out of the core simulation loop. Work runs
## only while a debugger is attached ([method EngineDebugger.is_active]) and is
## throttled, so an unobserved build pays nothing.
##
## [codeblock]
## Netw LagComp <tree>/
##   ┠╴ entities             # prediction components stepped per tick
##   ┠╴ timelines            # rewindable entities recorded per tick
##   ┠╴ corrections_rate     # reconciliation snaps per second
##   ┠╴ replay_depth_max     # deepest replay window walked (high-water)
##   ┠╴ consumed_rate        # inputs the server consumed per second
##   ┠╴ input_loss_pct       # lost inputs over the sample window
##   ┠╴ pending_actions      # actions queued awaiting readiness
##   ┠╴ fallbacks_rate       # state-ready actions resolved best-effort per second
##   ┖╴ effects_armed        # optimistic effects awaiting confirm or deny
## [/codeblock]
##
## The server populates the recording and consumption counters, so a pure client
## tree reads zero for those. Reach the data source through
## [method LagCompensation.metrics], never these monitors, in non-debug code.
class_name LagCompensationMonitor
extends Node

# Seconds between samples. Fast enough for a live graph, slow enough that the
# per-sample read is lost in the noise.
const _SAMPLE_INTERVAL := 0.25

# Cumulative counters delta-d into per-second rates.
const _RATE_KEYS: Array[StringName] = [
	&"corrections",
	&"consumed",
	&"missing",
	&"gate_fallbacks",
]

var _trees: Array[MultiplayerTree] = []
var _registered_ids: Dictionary[StringName, bool] = { }
# Latest display values per category, read by the registered callables.
var _latest_data: Dictionary[String, Dictionary] = { }
# Previous cumulative counters per category, for rate computation.
var _prev: Dictionary[String, Dictionary] = { }
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


## Tracks [param mt] so its [LagCompensation] is sampled each interval.
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
		var service := mt.get_service(LagCompensation) as LagCompensation
		if not service:
			continue
		_sample_tree(_category(mt), service.metrics(), elapsed)


func _sample_tree(category: String, metrics: Dictionary, elapsed: float) -> void:
	var prev: Dictionary = _prev.get(category, { })
	var rates: Dictionary = { }
	for key in _RATE_KEYS:
		var cur := int(metrics.get(key, 0))
		rates[key] = float(cur - int(prev.get(key, cur))) / maxf(elapsed, 0.0001)
		prev[key] = cur
	_prev[category] = prev

	var consumed_rate: float = rates[&"consumed"]
	var missing_rate: float = rates[&"missing"]
	var total_rate := consumed_rate + missing_rate

	_latest_data[category] = {
		&"entities": metrics.get(&"entities", 0),
		&"timelines": metrics.get(&"timelines", 0),
		&"corrections_rate": rates[&"corrections"],
		&"replay_depth_max": metrics.get(&"max_replay_depth", 0),
		&"consumed_rate": consumed_rate,
		&"input_loss_pct": (missing_rate / total_rate * 100.0) if total_rate > 0.0 else 0.0,
		&"pending_actions": metrics.get(&"pending_actions", 0),
		&"fallbacks_rate": rates[&"gate_fallbacks"],
		&"effects_armed": metrics.get(&"effects_armed", 0),
	}
	_ensure_registered(category)


func _ensure_registered(category: String) -> void:
	var store: Dictionary = _latest_data[category]
	for key in store.keys():
		var id := StringName("%s/%s" % [category, key])
		if _registered_ids.has(id):
			continue
		var metric_key: StringName = key
		_reg(id, func() -> Variant: return _latest_data.get(category, { }).get(metric_key, 0))


func _reg(id: StringName, callable: Callable) -> void:
	if _registered_ids.has(id):
		return
	if not Performance.has_custom_monitor(id):
		Performance.add_custom_monitor(id, callable, [], Performance.MONITOR_TYPE_QUANTITY)
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
	return "Netw LagComp %s" % mt.get_tree_name()
