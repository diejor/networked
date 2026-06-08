## Server-owned membership and per-peer visibility for one interest slice.
##
## A layer combines [member entities], [member viewers], and
## [member policy]. The server uses that state to decide which peers
## can see each entity. Entity membership never crosses the wire.
##
## [br][br]
## Pick signals by gameplay question:
## [br]- Server authority: [signal interest_enter] /
## [signal interest_exit].
## [br]- Local client view: [signal entity_visible] /
## [signal entity_hidden].
## [br]- Owner awareness: [signal NetwEntity.observer_entered] /
## [signal NetwEntity.observer_left].
##
## [br][br]
## Scene-level admission is separate and should happen before adding
## generic viewers for entities inside a scene. A generic layer can
## refine visibility under an already-visible scene; it should not be
## used to materialize the scene root.
## [codeblock]
## # Server: decide who can see the target.
## var sight := server_tree.interest.layer(&"sight")
## sight.add_entity(target_entity)
## sight.add_viewer(observer_peer_id)
##
## # Observer client: react to what this peer can see.
## var sight := Netw.ctx(self).interest.layer(&"sight")
## sight.entity_visible.connect(func(entity):
##     add_marker(entity.owner)
## )
##
## # Owner client: react to who can see this entity.
## Netw.ctx(self).entity.observer_entered.connect(func(layer_id, peer_id):
##     show_seen_by(peer_id)
## )
## [/codeblock]
class_name NetwInterestLayer
extends RefCounted

## Composition rule for the per-peer verdict.
enum Policy {
	## Peers in [member viewers] see [member entities]; outsiders do
	## not.
	HIDE_FROM_OUTSIDERS,
	## Peers in [member viewers] do [b]not[/b] see [member entities];
	## outsiders do.
	HIDE_FROM_INSIDERS,
}

## Emitted on the server when [param entity] becomes visible to
## [param peer_id] through this layer.
signal interest_enter(entity: NetwEntity, peer_id: int)

## Emitted on the server when [param entity] stops being visible to
## [param peer_id] through this layer.
signal interest_exit(entity: NetwEntity, peer_id: int)

## Emitted on a client when the local peer can see [param entity]
## through this layer.
signal entity_visible(entity: NetwEntity)

## Emitted on a client when the local peer stops seeing [param entity]
## through this layer.
signal entity_hidden(entity: NetwEntity)

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

## Emitted when the bound [InterestGate] detaches.
signal gate_unbound(gate: Object)

## Stable id used by [NetwInterest] to index this layer.
var layer_id: StringName

## Composition policy. See [enum Policy].
var policy: Policy = Policy.HIDE_FROM_OUTSIDERS

## Peer ids participating in this layer.
var viewers: Dictionary[int, bool] = { }

var _entities: Dictionary[NetwEntity, bool] = { }

## Entity set for this layer.
##
## On the server, this is every entity registered through
## [method add_entity]. On a client, this is every entity currently
## admitted to this layer for the local peer.
var entities: Dictionary[NetwEntity, bool]:
	get:
		return _entities

## Per-(entity, peer) transition cache used by [method drive_now].
var driver: InterestDriver = InterestDriver.new()

var _service_ref: WeakRef
var _bound_gate_ref: WeakRef


func _init(id: StringName = &"", service: Object = null) -> void:
	layer_id = id
	if service != null:
		_service_ref = weakref(service)


## Replaces [member policy]. Returns [code]true[/code] when changed.
func set_policy(value: Policy) -> bool:
	if policy == value:
		return false
	policy = value
	var s := _service()
	if s:
		s._on_layer_policy_changed(self)
	return true


## Adds [param peer_id] to [member viewers]. Idempotent. Returns
## [code]true[/code] when the set changed.
##
## [param peer_id] must be non-zero.
func add_viewer(peer_id: int) -> bool:
	assert(
		peer_id != 0,
		"NetwInterestLayer.add_viewer: peer_id must be non-zero",
	)
	if viewers.has(peer_id):
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


## Enrolls [param entity] in this layer. Idempotent. Server authoritative.
## Returns [code]false[/code] on clients.
##
## [param entity] must be non-null and own a live root node.
func add_entity(entity: NetwEntity) -> bool:
	assert(
		entity != null,
		"NetwInterestLayer.add_entity: entity is null",
	)
	assert(
		is_instance_valid(entity.owner),
		"NetwInterestLayer.add_entity: entity.owner is freed",
	)
	var s := _service()
	if s and not s._is_server():
		return false
	if _entities.has(entity):
		return false
	_entities[entity] = true
	entity_added.emit(entity)
	if s:
		s._on_layer_entity_changed(self, entity, true)
	return true


## Removes [param entity] from this layer. Emits exits first. Server
## authoritative. Returns [code]false[/code] on clients.
##
## [param entity] must be non-null; passing an unknown entity is a
## no-op for idempotent teardown.
func remove_entity(entity: NetwEntity) -> bool:
	assert(
		entity != null,
		"NetwInterestLayer.remove_entity: entity is null",
	)
	var s := _service()
	if s and not s._is_server():
		return false
	if not _entities.has(entity):
		return false
	var prev_view := driver.cached_view_for(entity)
	for peer_id: int in prev_view:
		if prev_view[peer_id]:
			interest_exit.emit(entity, peer_id)
			entity.interest_exit.emit(peer_id)
	driver.forget(entity)
	_entities.erase(entity)
	entity_removed.emit(entity)
	if s:
		s._on_layer_entity_changed(self, entity, false)
	return true


## Returns [code]true[/code] when [param entity] is in this layer.
func has_entity(entity: NetwEntity) -> bool:
	return _entities.has(entity)


# Idempotent client-side membership path for bound gates. Unlike the
# unbound RPC transition sink, this notifies InterestService so local
# synchronizer visibility filters are installed.
func _client_track_entity(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestLayer._client_track_entity: entity is null",
	)
	if _entities.has(entity):
		return
	_entities[entity] = true
	entity_added.emit(entity)
	var s := _service()
	if s:
		s._on_layer_entity_changed(self, entity, true)
	entity_visible.emit(entity)


# Idempotent client-side counterpart to [method _client_track_entity].
func _client_untrack_entity(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestLayer._client_untrack_entity: entity is null",
	)
	if not _entities.has(entity):
		return
	_entities.erase(entity)
	entity_removed.emit(entity)
	driver.forget(entity)
	var s := _service()
	if s:
		s._on_layer_entity_changed(self, entity, false)
	entity_hidden.emit(entity)


# Idempotent client-side admit. Adds [param entity] to [member entities]
# and emits [signal entity_visible]. Used by unbound-layer RPC relay.
func _client_admit(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestLayer._client_admit: entity is null",
	)
	if _entities.has(entity):
		return
	_entities[entity] = true
	entity_visible.emit(entity)


# Idempotent client-side revoke. Removes [param entity] from
# [member entities] and emits [signal entity_hidden].
func _client_revoke(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestLayer._client_revoke: entity is null",
	)
	if not _entities.has(entity):
		return
	_entities.erase(entity)
	entity_hidden.emit(entity)


## Returns the cached verdict for [param entity] and [param peer_id].
func is_visible_to(entity: NetwEntity, peer_id: int) -> bool:
	return driver.cached_verdict(entity, peer_id)


## Computes transitions for [param peers] without engine side effects.
func drive_now(peers: Array[int]) -> InterestDriver.Result:
	var result := driver.compute(_entities, peers, policy, viewers)
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


## Returns viewer ids in the format used by
## [method InterestGate.apply_snapshot].
func viewers_packed() -> PackedInt32Array:
	var out: PackedInt32Array = []
	for p: int in viewers.keys():
		out.append(p)
	return out


func _emit_transitions(result: InterestDriver.Result) -> void:
	for t in result.hide_transitions:
		interest_exit.emit(t.entity, t.peer)
		t.entity.interest_exit.emit(t.peer)
	for t in result.show_transitions:
		interest_enter.emit(t.entity, t.peer)
		t.entity.interest_enter.emit(t.peer)


func _service() -> InterestService:
	return _service_ref.get_ref() as InterestService if _service_ref else null


## Associates [param gate] with this layer for client-side admission.
##
## A bound gate mirrors [member viewers] and [member policy] to clients.
## It does not replicate [member entities].
func bind_gate(gate: Object) -> void:
	if gate == null:
		return
	var current := _bound_gate_ref.get_ref() if _bound_gate_ref else null
	if current and current != gate:
		push_error(
			"NetwInterestLayer[%s]: another gate is already bound"
			% [String(layer_id)],
		)
		return
	_bound_gate_ref = weakref(gate)
	gate_bound.emit(gate)


## Detaches the bound [InterestGate], if any.
func unbind_gate() -> void:
	var current := _bound_gate_ref.get_ref() if _bound_gate_ref else null
	_bound_gate_ref = null
	if current:
		gate_unbound.emit(current)


## Returns the bound [InterestGate], or [code]null[/code].
func bound_gate() -> Object:
	return _bound_gate_ref.get_ref() if _bound_gate_ref else null
