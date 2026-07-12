## Developer-only topology validation for the Networked addon.
##
## Inspects the entity topology of a player node: cache vs live synchronizer
## diff, and authority/identity checks sourced from the [NetwEntity] record
## rather than any single synchronizer node.
## [br][br]
## An instance so the checks are steppable (no statics, per the debugger's
## no-statics invariant). Owned by [TopologyNetValidator].
class_name TopologyValidator
extends RefCounted

## Returns the minimum expected [MultiplayerSynchronizer] count for [param node].
## [br][br]
## The addon requires no native synchronizer of its own. Replication is
## server-authored through the pipeline, so this floor is always [code]0[/code].
## Game-authored synchronizers are not counted here.
func expected_sync_count(_node: Node) -> int:
	return 0


## Validates the synchronizer topology of [param node].
## [br][br]
## Clears and rebuilds the cache as part of the check. Do not call on a live
## frame-critical path, this is a diagnostic tool.
## [br][br]
## Returns a [Dictionary]:
## [codeblock]
## Dictionary
##  ┠╴ok (bool)                            # true only when all checks pass
##  ┠╴errors (Array[String])               # list of failure descriptions
##  ┠╴live_count (int)                     # live synchronizer count
##  ┖╴expected_min (int)                   # minimum expected count
## [/codeblock]
func validate_node(node: Node) -> Dictionary:
	var errors: Array[String] = []
	var expected_min := expected_sync_count(node)

	SynchronizersCache.clear_cache(node)
	var live := SynchronizersCache.get_synchronizers(node)

	if live.size() < expected_min:
		errors.append(
			"live=%d < expected>=%d on '%s'" % \
					[live.size(), expected_min, node.name],
		)

	var diff := cache_diff(node)
	if not diff["match"]:
		errors.append(
			("cache/live mismatch: cached=%d live=%d | " +
					"only_cached=%s only_live=%s") % [
				diff["cached_count"],
				diff["live_count"],
				str(diff["only_in_cache"]),
				str(diff["only_in_live"]),
			],
		)

	errors.append_array(_check_identity(node))
	errors.append_array(_check_authority(node))

	return {
		"ok": errors.is_empty(),
		"errors": errors,
		"live_count": live.size(),
		"expected_min": expected_min,
	}


## Compares the current cached synchronizer list against a fresh traversal.
## [br][br]
## Returns a [Dictionary]:
## [codeblock]
## Dictionary
##  ┠╴match (bool)                         # true if cached & live sets match
##  ┠╴cached_count (int)
##  ┠╴live_count (int)
##  ┠╴only_in_cache (Array[String])        # names in cache but not in live
##  ┖╴only_in_live (Array[String])         # names in live but not in cache
## [/codeblock]
func cache_diff(node: Node) -> Dictionary:
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


func _check_identity(node: Node) -> Array[String]:
	var errs: Array[String] = []
	var entity := NetwEntity.of(node)
	if entity == null:
		return errs
	if entity.entity_id.is_empty():
		errs.append(
			(
					"spawned entity has no identity. Wrap your " +
					"spawn_function with NetwEntity.wrap_spawn or bind " +
					"identity before returning"
			),
		)
	return errs


func _check_authority(node: Node) -> Array[String]:
	var errs: Array[String] = []
	var expected := _get_expected_authority(node)
	if expected == 0:
		return errs

	var actual := node.get_multiplayer_authority()

	if actual != expected:
		errs.append(
			"Authority mismatch on '%s': expected=%d actual=%d. " % \
					[node.name, expected, actual] +
			"Multiplayer authority was not correctly assigned during spawn.",
		)
	return errs


func _get_expected_authority(node: Node) -> int:
	var entity := NetwEntity.of(node)
	if entity == null:
		return NetwEntity.parse_peer(node.name)

	var controller := entity.controller
	if controller == 0:
		return MultiplayerPeer.TARGET_PEER_SERVER
	return controller
