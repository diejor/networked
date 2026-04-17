## Developer-only topology validation for the Networked addon.
##
## Contains static helpers that inspect the synchronizer topology of a player node:
## expected vs actual counts, cache vs live diff, and virtual property constraint checks.
##
## [b]Never import this file from production components.[/b]
## (SaveComponent, LobbySynchronizer, ClientComponent, etc.)
##
## Use from: tests, [code]@tool[/code] scripts, debugger panels.
class_name TopologyValidator
extends RefCounted


## Returns the minimum expected [MultiplayerSynchronizer] count for [param node].
##
## Counts standard components present as children:
## - [ClientComponent] → 1 (SpawnSynchronizer)
## - [SaveComponent]   → +1 (SaveSynchronizer)
##
## Does not count user-defined synchronizers; this is a minimum floor only.
static func expected_sync_count(node: Node) -> int:
	var n := 0
	if node.get_node_or_null("%ClientComponent"):
		n += 1
	if node.get_node_or_null("%SaveComponent"):
		n += 1
	return n


## Validates the synchronizer topology of [param node].
##
## Clears and rebuilds the cache as part of the check. Do not call on a live
## frame-critical path — this is a diagnostic tool.
##
## Returns a [Dictionary]:
## [codeblock]
## {
##   ok:           bool,           # true only when all checks pass
##   errors:       Array[String],  # human-readable descriptions of failures
##   live_count:   int,            # synchronizers found by fresh traversal
##   expected_min: int,            # minimum expected count
## }
## [/codeblock]
static func validate_node(node: Node) -> Dictionary:
	var errors: Array[String] = []
	var expected_min := expected_sync_count(node)

	SynchronizersCache.clear_cache(node)
	var live := SynchronizersCache.get_synchronizers(node)

	if live.size() < expected_min:
		errors.append(
			"live=%d < expected>=%d on '%s'" % [live.size(), expected_min, node.name]
		)

	var diff := cache_diff(node)
	if not diff["match"]:
		errors.append(
			"cache/live mismatch: cached=%d live=%d | only_cached=%s only_live=%s" % [
				diff["cached_count"], diff["live_count"],
				str(diff["only_in_cache"]), str(diff["only_in_live"]),
			]
		)

	var save_comp: SaveComponent = node.get_node_or_null("%SaveComponent")
	if save_comp:
		errors.append_array(_check_virtual_props(save_comp))

	return {ok = errors.is_empty(), errors = errors,
			live_count = live.size(), expected_min = expected_min}


## Compares the current cached synchronizer list against a fresh traversal.
##
## Returns a [Dictionary]:
## [codeblock]
## {
##   match:        bool,           # true when cached and live sets are identical
##   cached_count: int,
##   live_count:   int,
##   only_in_cache: Array[String], # names present in cache but not in live traversal
##   only_in_live:  Array[String], # names present in live traversal but not in cache
## }
## [/codeblock]
static func cache_diff(node: Node) -> Dictionary:
	var cached_names: Array[String] = []
	if node.has_meta(SynchronizersCache.META_KEY):
		var cached: Array[MultiplayerSynchronizer] = []
		cached.assign(node.get_meta(SynchronizersCache.META_KEY))
		for s in cached:
			cached_names.append(s.name if is_instance_valid(s) else "<freed>")

	SynchronizersCache.clear_cache(node)
	var live_names: Array[String] = []
	for s in SynchronizersCache.get_synchronizers(node):
		live_names.append(s.name)

	var only_cached := cached_names.filter(func(n: String): return n not in live_names)
	var only_live   := live_names.filter(func(n: String): return n not in cached_names)
	return {
		match         = only_cached.is_empty() and only_live.is_empty(),
		cached_count  = cached_names.size(),
		live_count    = live_names.size(),
		only_in_cache = only_cached,
		only_in_live  = only_live,
	}


static func _check_virtual_props(save_comp: SaveComponent) -> Array[String]:
	var errs: Array[String] = []
	if not save_comp.save_synchronizer or not save_comp.save_synchronizer.replication_config:
		return errs
	var cfg := save_comp.save_synchronizer.replication_config
	for prop: NodePath in cfg.get_properties():
		if cfg.property_get_watch(prop):
			errs.append(
				"virtual property '%s' has watch=true — C++ cannot resolve against root_path '.'" \
				% str(prop)
			)
	return errs
