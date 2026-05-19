## A named slice of the interest graph: a [member viewers] set, an
## [member entities] set, and a composition [enum Policy].
##
## Layers are the canonical state and the canonical mutation API.
## [member entities] is server-only and never crosses the wire;
## [member viewers] and [member policy] do, but only when an
## [InterestGate] is bound to the layer via [method bind_gate].
## The owning [InterestService] batches mutations and flushes once
## per frame, applying engine-side visibility through
## [method MultiplayerSynchronizer.set_visibility_for] on entity
## synchronizers and on the bound gate.
##
## [br][br]
## Server reacts via [signal interest_enter] / [signal interest_exit];
## clients react via Godot's
## [signal MultiplayerSynchronizer.visibility_changed] on the
## entity synchronizers the server admitted them to.
## [codeblock]
## var arena := Netw.ctx(self).interest.layer(&"arena")
## arena.add_entity(player_entity)
## arena.add_viewer(player.peer_id)
## arena.interest_enter.connect(_on_seen_by)
## [/codeblock]
class_name NetwInterestLayer
extends RefCounted


## Composition rule for the per-peer verdict.
enum Policy {
	## Peers in [member viewers] see [member entities]; outsiders do
	## not.
	HIDE_FROM_OUTSIDERS,
	## Peers in [member viewers] do [b]not[/b] see [member entities];
	## outsiders do. Use for stealth-style bubbles.
	HIDE_FROM_INSIDERS,
}


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

## Emitted when an [InterestGate] binds to this layer.
signal gate_bound(gate: Object)

## Emitted when the bound gate is detached.
signal gate_unbound(gate: Object)


## Stable id used by [NetwInterest] to index this layer.
var layer_id: StringName

## Composition policy. See [enum Policy].
var policy: int = Policy.HIDE_FROM_OUTSIDERS

## Peer ids participating in this layer.
var viewers: Dictionary[int, bool] = {}

## Entities participating in this layer.
var entities: Dictionary[NetwEntity, bool] = {}

## Per-(entity, peer) transition cache used by [method drive_now].
var driver: InterestDriver = InterestDriver.new()


var _service_ref: WeakRef
var _bound_gate_ref: WeakRef


func _init(id: StringName = &"", service: Object = null) -> void:
	layer_id = id
	if service != null:
		_service_ref = weakref(service)


## Replaces [member policy]. Returns [code]true[/code] when changed.
func set_policy(value: int) -> bool:
	if policy == value:
		return false
	policy = value
	var s := _service()
	if s:
		s._on_layer_policy_changed(self)
	return true


## Adds [param peer_id] to [member viewers]. Idempotent. Returns
## [code]true[/code] when the set changed.
func add_viewer(peer_id: int) -> bool:
	if peer_id == 0 or viewers.has(peer_id):
		return false
	viewers[peer_id] = true
	viewer_added.emit(peer_id)
	var s := _service()
	if s:
		s._on_layer_viewer_changed(self, peer_id, true)
	return true


## Removes [param peer_id] from [member viewers]. Idempotent.
func remove_viewer(peer_id: int) -> bool:
	if not viewers.has(peer_id):
		return false
	viewers.erase(peer_id)
	viewer_removed.emit(peer_id)
	var s := _service()
	if s:
		s._on_layer_viewer_changed(self, peer_id, false)
	return true


## Returns [code]true[/code] when [param peer_id] is a viewer.
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Enrolls [param entity] in this layer. Idempotent.
func add_entity(entity: NetwEntity) -> bool:
	if entity == null or not is_instance_valid(entity.owner):
		return false
	if entities.has(entity):
		return false
	entities[entity] = true
	entity_added.emit(entity)
	var s := _service()
	if s:
		s._on_layer_entity_changed(self, entity, true)
	return true


## Removes [param entity] from this layer. Emits [signal interest_exit]
## for every peer that previously saw it.
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
	var s := _service()
	if s:
		s._on_layer_entity_changed(self, entity, false)
	return true


## Returns [code]true[/code] when [param entity] is in this layer.
func has_entity(entity: NetwEntity) -> bool:
	return entities.has(entity)


## Returns the cached verdict for [param entity] and [param peer_id].
func is_visible_to(entity: NetwEntity, peer_id: int) -> bool:
	return driver.cached_verdict(entity, peer_id)


## Computes and emits transitions for [param peers]. Tests use this
## to observe enter/exit without an [InterestService]; in production,
## the service drives this on dirty layers.
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


## Returns current viewer peer ids as a [PackedInt32Array] suitable
## for [method InterestGate.apply_snapshot].
func viewers_packed() -> PackedInt32Array:
	var out: PackedInt32Array = []
	for p: int in viewers.keys():
		out.append(p)
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


func _service() -> InterestService:
	return _service_ref.get_ref() as InterestService if _service_ref else null


## Associates an [InterestGate] with this layer. The gate's synced
## [code]viewers[/code] and [code]policy[/code] properties will be
## kept in step with this layer's state on the server, and Godot's
## spawn-sync / property replication will deliver them to clients.
## Errors if another gate is already bound.
func bind_gate(gate: Object) -> void:
	if gate == null:
		return
	var current := _bound_gate_ref.get_ref() if _bound_gate_ref else null
	if current and current != gate:
		push_error(
				"NetwInterestLayer[%s]: another gate is already bound"
				% [String(layer_id)])
		return
	_bound_gate_ref = weakref(gate)
	gate_bound.emit(gate)


## Detaches the bound gate, if any.
func unbind_gate() -> void:
	var current := _bound_gate_ref.get_ref() if _bound_gate_ref else null
	_bound_gate_ref = null
	if current:
		gate_unbound.emit(current)


## Returns the bound [InterestGate], or [code]null[/code].
func bound_gate() -> Object:
	return _bound_gate_ref.get_ref() if _bound_gate_ref else null
