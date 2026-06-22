@tool
## Exposes [InterestService] occupancy as Godot [Performance] monitors, with a
## tree-wide group per [MultiplayerTree] and one group per [NetwInterestLayer].
##
## This is a presentation adapter managed by the [NetworkedDebugReporter]. It pulls
## [method InterestService.monitor_snapshot] and
## [method NetwInterestLayer.monitor_snapshot], turning the cumulative transition
## counters into live rates. Work runs only while a debugger is attached
## ([method EngineDebugger.is_active]) and is throttled, and the per-layer
## [code]visible_edges[/code] read is O(1), so an unobserved build pays nothing and
## an observed one never walks the visibility matrix.
##
## [codeblock]
## Netw Interest <tree>/
##   ┠╴ layers            # active layers
##   ┠╴ entities_filtered # entities with a visibility filter
##   ┠╴ visible_edges     # admitted (entity, peer) pairs, all layers
##   ┠╴ transitions_rate  # visibility churn per second
##   ┠╴ dirty_entities    # entities pending a recompute
##   ┖╴ relay_backlog     # unbound-layer transitions awaiting reconcile
## Netw Interest <tree> · <layer_id>/
##   ┠╴ viewers           # viewer set size
##   ┠╴ entities          # entity set size
##   ┠╴ visible_edges     # admitted pairs for this layer
##   ┖╴ transitions_rate  # this layer's churn per second
## [/codeblock]
##
## Interest is server authoritative, so a pure client tree reads near zero. Reach
## the data source through [method InterestService.monitor_snapshot] in non-debug
## code, never these monitors.
class_name InterestMonitor
extends Node

# Seconds between samples. Fast enough for a live graph, slow enough that the
# per-sample read is lost in the noise.
const _SAMPLE_INTERVAL := 0.25

var _trees: Array[MultiplayerTree] = []
var _registered_ids: Dictionary[StringName, bool] = { }
# Latest display values per category, read by the registered callables. Its keys
# double as the live-category set used to prune vanished layers.
var _latest_data: Dictionary[String, Dictionary] = { }
# Previous cumulative transition counts per category, for rate computation.
var _prev: Dictionary[String, int] = { }
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


## Tracks [param mt] so its [InterestService] is sampled each interval.
func register_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		_trees.append(mt)


## Stops tracking [param mt] and removes its tree-wide and per-layer monitors.
func unregister_tree(mt: MultiplayerTree) -> void:
	_trees.erase(mt)
	if is_instance_valid(mt):
		_drop_under(_tree_category(mt))


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
		var service := mt.get_service(InterestService) as InterestService
		if not service:
			continue
		var tree_category := _tree_category(mt)
		var snap := service.monitor_snapshot()
		_store(tree_category, {
			&"layers": snap.get(&"layers", 0),
			&"entities_filtered": snap.get(&"entities_filtered", 0),
			&"visible_edges": snap.get(&"visible_edges", 0),
			&"transitions_rate": _rate(tree_category, int(snap.get(&"transitions_total", 0)), elapsed),
			&"dirty_entities": snap.get(&"dirty_entities", 0),
			&"relay_backlog": snap.get(&"relay_backlog", 0),
		})

		var live: Dictionary[String, bool] = { }
		for layer in service.all_layers():
			var layer_category := "%s · %s" % [tree_category, String(layer.layer_id)]
			live[layer_category] = true
			var lsnap := layer.monitor_snapshot()
			_store(layer_category, {
				&"viewers": lsnap.get(&"viewers", 0),
				&"entities": lsnap.get(&"entities", 0),
				&"visible_edges": lsnap.get(&"visible_edges", 0),
				&"transitions_rate": _rate(layer_category, int(lsnap.get(&"transitions_total", 0)), elapsed),
			})
		_prune_layers(tree_category, live)


# Computes a per-second rate from a cumulative counter, carrying the previous
# value per category. A counter reset (new session) clamps to zero, not negative.
func _rate(category: String, cumulative: int, elapsed: float) -> float:
	var prev := int(_prev.get(category, cumulative))
	_prev[category] = cumulative
	return maxf(0.0, float(cumulative - prev)) / maxf(elapsed, 0.0001)


func _store(category: String, values: Dictionary) -> void:
	_latest_data[category] = values
	for key in values.keys():
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


# Drops layer groups under tree_category that no longer have a live layer.
func _prune_layers(tree_category: String, live: Dictionary) -> void:
	var layer_prefix := tree_category + " · "
	for category in _latest_data.keys():
		if category.begins_with(layer_prefix) and not live.has(category):
			_drop_category(category)


func _drop_category(category: String) -> void:
	var prefix := category + "/"
	for id in _registered_ids.keys():
		if String(id).begins_with(prefix):
			if Performance.has_custom_monitor(id):
				Performance.remove_custom_monitor(id)
			_registered_ids.erase(id)
	_latest_data.erase(category)
	_prev.erase(category)


# Drops the tree-wide group and every per-layer group beneath it.
func _drop_under(tree_category: String) -> void:
	for category in _latest_data.keys():
		if category == tree_category or category.begins_with(tree_category + " · "):
			_drop_category(category)


func _tree_category(mt: MultiplayerTree) -> String:
	return "Netw Interest %s" % mt.get_tree_name()
