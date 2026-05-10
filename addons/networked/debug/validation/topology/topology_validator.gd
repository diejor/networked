## Developer-only topology validation for the Networked addon.
##
## Contains static helpers that inspect the synchronizer topology of a player
## node: expected vs actual counts, cache vs live diff, and virtual property
## constraint checks.
## [br][br]
## [b]Never import this file from production components.[/b]
## (SaveComponent, SceneSynchronizer, SpawnerPlayerComponent, etc.)
## [br][br]
## Use from: tests, [code]@tool[/code] scripts, debugger panels.
class_name TopologyValidator
extends RefCounted


## Returns the minimum expected [MultiplayerSynchronizer] count for [param node].
## [br][br]
## Counts standard components present as children:
## [br]- [SpawnerPlayerComponent] -> 1 (extends MultiplayerSynchronizer)
## [br][br]
## Does not count user-defined synchronizers; this is a minimum floor only.
static func expected_sync_count(node: Node) -> int:
	var n := 0
	if SpawnerPlayerComponent.unwrap(node) != null:
		n += 1
	return n


## Validates the synchronizer topology of [param node].
## [br][br]
## Clears and rebuilds the cache as part of the check. Do not call on a live
## frame-critical path, this is a diagnostic tool.
## [br][br]
## Returns a [Dictionary]:
## [codeblock]
## {
##   "ok":           bool,           # true only when all checks pass
##   "errors":       Array[String],  # human-readable descriptions of failures
##   "live_count":   int,            # synchronizers found by fresh traversal
##   "expected_min": int,            # minimum expected count
## }
## [/codeblock]
static func validate_node(node: Node) -> Dictionary:
	var errors: Array[String] = []
	var expected_min := expected_sync_count(node)

	SynchronizersCache.clear_cache(node)
	var live := SynchronizersCache.get_synchronizers(node)

	if live.size() < expected_min:
		errors.append(
			"live=%d < expected>=%d on '%s'" % \
			[live.size(), expected_min, node.name]
		)

	var diff := cache_diff(node)
	if not diff["match"]:
		errors.append(
			"cache/live mismatch: cached=%d live=%d | " + \
			"only_cached=%s only_live=%s" % [
				diff["cached_count"], diff["live_count"],
				str(diff["only_in_cache"]), str(diff["only_in_live"]),
			]
		)

	var save_comp: SaveComponent = node.get_node_or_null("%SaveComponent")
	if save_comp:
		errors.append_array(_check_save_component(save_comp))

	var client_comp := SpawnerPlayerComponent.unwrap(node)
	if client_comp:
		errors.append_array(_check_spawner_component(client_comp))

	errors.append_array(_check_authority(node))
	errors.append_array(_check_server_authority_synchronizer(node))

	return {
		"ok": errors.is_empty(),
		"errors": errors,
		"live_count": live.size(),
		"expected_min": expected_min
	}


## Compares the current cached synchronizer list against a fresh traversal.
## [br][br]
## Returns a [Dictionary]:
## [codeblock]
## {
##   "match":        bool,           # true when cached and live sets match
##   "cached_count": int,
##   "live_count":   int,
##   "only_in_cache": Array[String], # names in cache but not in live
##   "only_in_live":  Array[String], # names in live but not in cache
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

	var only_cached := cached_names.filter(
		func(n: String): return n not in live_names
	)
	var only_live := live_names.filter(
		func(n: String): return n not in cached_names
	)
	return {
		"match": only_cached.is_empty() and only_live.is_empty(),
		"cached_count": cached_names.size(),
		"live_count": live_names.size(),
		"only_in_cache": only_cached,
		"only_in_live": only_live,
	}


static func _check_save_component(save_comp: SaveComponent) -> Array[String]:
	var errs: Array[String] = []

	var config := save_comp.replication_config
	if not config or config.get_properties().is_empty():
		errs.append(
			"SaveComponent on '%s' has 0 replication properties. " % \
			[save_comp.owner.name if save_comp.owner else "?"] + \
			"Check the Replication panel in the Editor and ensure " + \
			"instantiate() has been called."
		)

	if config:
		pass

	if save_comp.database and not save_comp.table_name.is_empty():
		var tracked: Array[StringName] = save_comp._properties.keys()
		var registered: Array[StringName] = \
			save_comp.database.get_registered_columns(save_comp.table_name)
		if not registered.is_empty():
			var only_tracked := tracked.filter(
				func(c: StringName) -> bool: return c not in registered
			)
			var only_registered := registered.filter(
				func(c: StringName) -> bool: return c not in tracked
			)
			if not only_tracked.is_empty() or not only_registered.is_empty():
				errs.append(
					("Schema drift on '%s' table='%s': only_in_sync=%s " + \
					"only_in_db=%s") % [
						save_comp.owner.name if save_comp.owner else "?",
						save_comp.table_name,
						str(only_tracked), str(only_registered),
					]
				)

	return errs


static func _check_spawner_component(
	spawner: SpawnerPlayerComponent
) -> Array[String]:
	var errs: Array[String] = []
	if spawner.root_path == NodePath(""):
		errs.append(
			"SpawnerPlayerComponent.root_path is empty on '%s'. " % \
			[spawner.owner.name] + \
			"get_path_to(spawner.owner) was likely called before the " + \
			"player entered the scene tree."
		)
	return errs


static func _check_authority(node: Node) -> Array[String]:
	var errs: Array[String] = []
	var spawner := SpawnerComponent.unwrap(node)
	var expected := _get_expected_authority(node, spawner)
	if expected == 0:
		return errs

	var actual := node.get_multiplayer_authority()

	if actual != expected:
		errs.append(
			"Authority mismatch on '%s': expected=%d actual=%d. " % \
			[node.name, expected, actual] + \
			"Multiplayer authority was not correctly assigned during spawn."
		)
	return errs


static func _get_expected_authority(
	node: Node,
	spawner: SpawnerComponent
) -> int:
	if not spawner:
		return SpawnerComponent.parse_authority(node.name)

	match spawner.authority_mode:
		SpawnerComponent.AuthorityMode.SERVER:
			return MultiplayerPeer.TARGET_PEER_SERVER
		SpawnerComponent.AuthorityMode.CLIENT:
			return SpawnerComponent.parse_authority(node.name)
	return 0


static func _check_server_authority_synchronizer(
	node: Node
) -> Array[String]:
	var errs: Array[String] = []
	var has_server_sync := false
	for sync: MultiplayerSynchronizer in \
			SynchronizersCache.get_synchronizers(node):
		if sync.get_multiplayer_authority() == 1:
			has_server_sync = true
			break
	if not has_server_sync:
		errs.append(
			"No server-authoritative MultiplayerSynchronizer on '%s'. " % \
			[node.name] + \
			"Scene visibility requires at least one synchronizer " + \
			"with authority=1 so the server can control replication."
		)
	return errs
