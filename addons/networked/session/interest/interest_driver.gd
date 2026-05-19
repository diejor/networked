## Pure transition computer for [NetwInterestLayer].
##
## Holds the per-(entity, peer) verdict cache. [method compute] takes
## the current entity set, peer list, and policy state, returns the
## transitions that would land the cache in the new state, and leaves
## the cache untouched until [method commit] is called. Splitting
## compute from commit lets the caller apply engine-side effects
## (filter updates, signal emission) in the right order before the
## cached state advances.
##
## [b]Engine-free:[/b] depends only on [NetwEntity] and
## [InterestPolicy]. Unit tests construct a driver directly without
## any multiplayer peer.
##
## [codeblock]
##     var driver := InterestDriver.new()
##     var result := driver.compute(entities, peers, kind, viewers)
##     emit_transitions(result.hide_transitions, result.show_transitions)
##     driver.commit(result)
## [/codeblock]
class_name InterestDriver
extends RefCounted


## Output of one [method compute] pass. Transitions are sorted so the
## caller can iterate in engine-safe order: hides deep-first to avoid
## Godot issue #68508, shows shallow-first so spawn packets arrive
## before child spawn packets reference them.
class Result:
	extends RefCounted
	## Per-(entity, peer) hide transitions, deep-first.
	var hide_transitions: Array = []
	## Per-(entity, peer) show transitions, shallow-first.
	var show_transitions: Array = []
	## Per-(sync, peer) hide tuples for visibility updates, deep-first.
	var sync_hides: Array = []
	## Per-(sync, peer) show tuples for visibility updates, shallow-first.
	var sync_shows: Array = []
	## Full new visibility state: [code]{entity: {peer: bool}}[/code].
	var new_state: Dictionary = {}


var _state: Dictionary = {}


## Returns the cached verdict for [param entity] under [param peer_id]
## without recomputing.
func cached_verdict(entity: NetwEntity, peer_id: int) -> bool:
	var per_entity: Dictionary = _state.get(entity, {})
	return per_entity.get(peer_id, false)


## Returns every peer currently cached as visible for [param entity].
## Used to emit exit transitions before the entity leaves its layer.
func cached_view_for(entity: NetwEntity) -> Dictionary:
	return _state.get(entity, {}).duplicate()


## Drops the cache entry for [param entity]. Returns the previous
## per-peer dict.
func forget(entity: NetwEntity) -> Dictionary:
	var prev: Dictionary = _state.get(entity, {})
	_state.erase(entity)
	return prev


## Returns the peer ids the driver has ever cached a verdict for.
## Used to drive hide transitions for peers removed from the viewer set.
func cached_peers() -> Array[int]:
	var seen: Dictionary[int, bool] = {}
	for entity in _state:
		var per_entity: Dictionary = _state[entity]
		for p: int in per_entity:
			seen[p] = true
	var out: Array[int] = []
	out.assign(seen.keys())
	return out


## Computes the transitions that would result from re-evaluating
## [param entities] against [param peers] under
## [code](kind, viewers)[/code]. Does not mutate the driver state;
## call [method commit] to advance.
func compute(
		entities: Dictionary,
		peers: Array[int],
		kind: NetwInterestLayer.Policy,
		viewers: Dictionary) -> Result:
	var result := Result.new()
	for entity: NetwEntity in entities:
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		_compute_entity(entity, peers, kind, viewers, result)
	result.sync_hides.sort_custom(_sync_deeper_first)
	result.sync_shows.sort_custom(_sync_shallower_first)
	result.hide_transitions.sort_custom(_entity_deeper_first)
	result.show_transitions.sort_custom(_entity_shallower_first)
	return result


func _compute_entity(
		entity: NetwEntity,
		peers: Array[int],
		kind: NetwInterestLayer.Policy,
		viewers: Dictionary,
		result: Result) -> void:
	# Off-tree owners and syncs cannot be ordered by [method
	# Node.get_path] (which the comparators call), and an off-tree
	# sync cannot be the target of [method
	# MultiplayerSynchronizer.update_visibility] anyway. Skip both so
	# the binding-apply phase only sees nodes the engine can act on.
	if not entity.owner.is_inside_tree():
		return
	var prev: Dictionary = _state.get(entity, {})
	var per_entity: Dictionary = {}
	result.new_state[entity] = per_entity
	for peer: int in peers:
		var now := InterestPolicy.verdict(kind, viewers, peer)
		per_entity[peer] = now
		var was: bool = prev.get(peer, false)
		if was == now:
			continue
		var transition := [entity, peer]
		if now:
			result.show_transitions.append(transition)
		else:
			result.hide_transitions.append(transition)
		for sync in entity.synchronizers():
			if not is_instance_valid(sync) or not sync.is_inside_tree():
				continue
			var tup := [sync, peer]
			if now:
				result.sync_shows.append(tup)
			else:
				result.sync_hides.append(tup)


## Adopts [param result.new_state] as the current cache. Call after
## engine-side effects and signals have been applied.
func commit(result: Result) -> void:
	_state = result.new_state


## Returns a shallow copy of the visibility cache for inspection.
## Structure: [code]{NetwEntity: {peer_id: bool}}[/code].
func dump() -> Dictionary:
	return _state.duplicate(true)


# Sort comparators. Depth is measured via Node path name count so a
# scripted scene tree and a runtime-built tree compare consistently.

func _sync_deeper_first(a: Array, b: Array) -> bool:
	return (a[0] as Node).get_path().get_name_count() \
			> (b[0] as Node).get_path().get_name_count()


func _sync_shallower_first(a: Array, b: Array) -> bool:
	return (a[0] as Node).get_path().get_name_count() \
			< (b[0] as Node).get_path().get_name_count()


func _entity_deeper_first(a: Array, b: Array) -> bool:
	return (a[0] as NetwEntity).owner.get_path().get_name_count() \
			> (b[0] as NetwEntity).owner.get_path().get_name_count()


func _entity_shallower_first(a: Array, b: Array) -> bool:
	return (a[0] as NetwEntity).owner.get_path().get_name_count() \
			< (b[0] as NetwEntity).owner.get_path().get_name_count()
