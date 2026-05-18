## Pure data model for one interest layer.
##
## Stores layer membership and viewer state, computes local visibility
## transitions, and emits the same entity-level signals used by
## [InterestSynchronizer]. It does not touch Godot replication APIs;
## [NetwInterest] and adapter nodes decide how engine visibility is
## applied.
class_name NetwInterestLayer
extends RefCounted


## Emitted when [param entity] becomes visible to [param peer_id].
signal interest_enter(entity: NetwEntity, peer_id: int)

## Emitted when [param entity] stops being visible to [param peer_id].
signal interest_exit(entity: NetwEntity, peer_id: int)

## Emitted when [param peer_id] is added to [member viewers].
signal viewer_added(peer_id: int)

## Emitted when [param peer_id] is removed from [member viewers].
signal viewer_removed(peer_id: int)

## Emitted when [param entity] joins this layer.
signal entity_added(entity: NetwEntity)

## Emitted when [param entity] leaves this layer.
signal entity_removed(entity: NetwEntity)


## Stable id used by [NetwInterest] to index this layer.
var layer_id: StringName

## Composition policy. Uses the same values as
## [enum InterestSynchronizer.Policy].
var policy: int = InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS

## Peer ids participating in this layer.
var viewers: Dictionary[int, bool] = {}

## Entities participating in this layer.
var entities: Dictionary[NetwEntity, bool] = {}

## Per-(entity, peer) transition cache.
var driver: InterestDriver = InterestDriver.new()


func _init(id: StringName = &"") -> void:
	layer_id = id


## Replaces [member policy]. Returns [code]true[/code] when changed.
func set_policy(value: int) -> bool:
	if policy == value:
		return false
	policy = value
	return true


## Adds [param peer_id] to [member viewers]. Idempotent.
func add_viewer(peer_id: int) -> bool:
	if peer_id == 0 or viewers.has(peer_id):
		return false
	viewers[peer_id] = true
	viewer_added.emit(peer_id)
	return true


## Removes [param peer_id] from [member viewers]. Idempotent.
func remove_viewer(peer_id: int) -> bool:
	if not viewers.has(peer_id):
		return false
	viewers.erase(peer_id)
	viewer_removed.emit(peer_id)
	return true


## Returns [code]true[/code] when [param peer_id] is a viewer.
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Adds [param entity] to [member entities]. Idempotent.
func add_entity(entity: NetwEntity) -> bool:
	if entity == null or not is_instance_valid(entity.owner):
		return false
	if entities.has(entity):
		return false
	entities[entity] = true
	entity_added.emit(entity)
	return true


## Removes [param entity] from [member entities]. Idempotent.
func remove_entity(entity: NetwEntity) -> bool:
	if entity == null or not entities.has(entity):
		return false
	var prev_view := driver.cached_view_for(entity)
	for peer_id: int in prev_view:
		if prev_view[peer_id]:
			interest_exit.emit(entity, peer_id)
			entity.interest_exit.emit(peer_id)
	driver.forget(entity)
	entities.erase(entity)
	entity_removed.emit(entity)
	return true


## Returns [code]true[/code] when [param entity] is in this layer.
func has_entity(entity: NetwEntity) -> bool:
	return entities.has(entity)


## Returns the cached verdict for [param entity] and [param peer_id].
func is_visible_to(entity: NetwEntity, peer_id: int) -> bool:
	return driver.cached_verdict(entity, peer_id)


## Computes and emits local transitions for [param peers].
func drive_now(peers: Array[int]) -> InterestDriver.Result:
	var result := driver.compute(entities, peers, policy, viewers)
	_emit_transitions(result)
	driver.commit(result)
	return result


## Returns the current policy verdict for [param peer_id].
func verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, viewers, peer_id)


## Returns a structured snapshot for debugging.
func debug_dump(peer_id: int = 0) -> Dictionary:
	return {
		"layer_id": String(layer_id),
		"policy": policy,
		"viewers": viewer_ids(),
		"entities": entities.size(),
		"peer_id": peer_id,
		"verdict": verdict_for(peer_id),
		"explanation": InterestPolicy.explain(policy, viewers, peer_id),
		"driver_cache": driver.dump(),
	}


## Returns current viewer peer ids.
func viewer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(viewers.keys())
	return out


func _emit_transitions(result: InterestDriver.Result) -> void:
	for t in result.hide_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_exit.emit(entity, peer)
		entity.interest_exit.emit(peer)
	for t in result.show_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_enter.emit(entity, peer)
		entity.interest_enter.emit(peer)
