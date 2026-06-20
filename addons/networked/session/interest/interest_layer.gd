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
## Returns the bound [InterestGate], or [code]null[/code].
func bound_gate() -> Object:
	return _bound_gate_ref.get_ref() if _bound_gate_ref else null


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
class InterestDriver:
	extends RefCounted

	## One per-entity visibility change emitted by [method compute].
	##
	## [member entity] and [member peer] identify the exact visibility edge.
	## [NetwInterestLayer] emits [signal interest_enter] or
	## [signal interest_exit] from these values after [method compute].
	## [codeblock]
	## var transition := result.show_transitions[0]
	## transition.entity.interest_enter.emit(transition.peer)
	## [/codeblock]
	class Transition:
		extends RefCounted
		## Entity whose visibility changed.
		var entity: NetwEntity
		## Peer id whose visibility changed.
		var peer: int


		func _init(e: NetwEntity = null, p: int = 0) -> void:
			entity = e
			peer = p


	## Output of one [method compute] pass.
	##
	## [member hide_transitions] and [member show_transitions] are sorted for
	## engine-safe application. [member new_state] is adopted by
	## [method commit] after [NetwInterestLayer] emits signals.
	## [codeblock]
	## var result := driver.compute(entities, peers, policy, viewers)
	## emit_transitions(result.hide_transitions, result.show_transitions)
	## driver.commit(result)
	## [/codeblock]
	class Result:
		extends RefCounted
		## Per-(entity, peer) hide transitions, deep-first.
		var hide_transitions: Array[Transition] = []
		## Per-(entity, peer) show transitions, shallow-first.
		var show_transitions: Array[Transition] = []
		## Full new visibility state: [code]{entity: {peer: bool}}[/code].
		var new_state: Dictionary = { }


	var _state: Dictionary = { }


	## Returns the cached verdict for [param entity] under [param peer_id]
	## without recomputing.
	func cached_verdict(entity: NetwEntity, peer_id: int) -> bool:
		var per_entity: Dictionary = _state.get(entity, { })
		return per_entity.get(peer_id, false)


	## Returns every peer currently cached as visible for [param entity].
	## Used to emit exit transitions before the entity leaves its layer.
	func cached_view_for(entity: NetwEntity) -> Dictionary:
		return _state.get(entity, { }).duplicate()


	## Drops the cache entry for [param entity]. Returns the previous
	## per-peer dict.
	func forget(entity: NetwEntity) -> Dictionary:
		var prev: Dictionary = _state.get(entity, { })
		_state.erase(entity)
		return prev


	## Returns the peer ids the driver has ever cached a verdict for.
	## Used to drive hide transitions for peers removed from the viewer set.
	func cached_peers() -> Array[int]:
		var seen: Dictionary[int, bool] = { }
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
			viewers: Dictionary,
	) -> Result:
		var result := Result.new()
		# Policy verdict depends only on (kind, viewers, peer), not on the
		# entity. Compute it once per peer and reuse across the layer.
		var verdict_by_peer: Dictionary = { }
		for peer: int in peers:
			verdict_by_peer[peer] = InterestPolicy.verdict(kind, viewers, peer)
		for entity: NetwEntity in entities:
			if not is_instance_valid(entity) \
					or not is_instance_valid(entity.owner):
				continue
			_compute_entity(entity, verdict_by_peer, result)
		result.hide_transitions.sort_custom(_transition_deeper_first)
		result.show_transitions.sort_custom(_transition_shallower_first)
		return result


	func _compute_entity(
			entity: NetwEntity,
			verdict_by_peer: Dictionary,
			result: Result,
	) -> void:
		# Off-tree owners and syncs cannot be ordered by [method
		# Node.get_path] (which the comparators call), and an off-tree
		# sync cannot be the target of [method
		# MultiplayerSynchronizer.update_visibility] anyway. Skip both so
		# the binding-apply phase only sees nodes the engine can act on.
		if not entity.owner.is_inside_tree():
			return
		var prev: Dictionary = _state.get(entity, { })
		var per_entity: Dictionary = { }
		result.new_state[entity] = per_entity
		for peer: int in verdict_by_peer:
			var now: bool = verdict_by_peer[peer]
			per_entity[peer] = now
			var was: bool = prev.get(peer, false)
			if was == now:
				continue
			var transition := Transition.new(entity, peer)
			if now:
				result.show_transitions.append(transition)
			else:
				result.hide_transitions.append(transition)


	## Adopts the new state from [param result] as the current cache. Call after
	## engine-side effects and signals have been applied.
	func commit(result: Result) -> void:
		_state = result.new_state


	## Returns a shallow copy of the visibility cache for inspection.
	## Structure: [code]{NetwEntity: {peer_id: bool}}[/code].
	func dump() -> Dictionary:
		return _state.duplicate(true)

	# Sort comparators. Depth is measured via Node path name count so a
	# scripted scene tree and a runtime-built tree compare consistently.


	func _transition_deeper_first(a: Transition, b: Transition) -> bool:
		return a.entity.owner.get_path().get_name_count() \
				> b.entity.owner.get_path().get_name_count()


	func _transition_shallower_first(a: Transition, b: Transition) -> bool:
		return a.entity.owner.get_path().get_name_count() \
				< b.entity.owner.get_path().get_name_count()


## Stateless verdict resolver for [NetwInterestLayer].
##
## Given a [enum NetwInterestLayer.Policy] and a viewer set, returns
## the per-peer visibility verdict. Every gate in the system - layer
## verdicts, per-entity filters, anchor admission - routes through
## this function so disagreement is a single-source bug.
##
## [method explain] returns a human-readable reason and is the first
## tool to reach for when a peer is visible or hidden when it should
## not be.
##
## [codeblock]
##     var k := NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS
##     InterestPolicy.verdict(k, viewers, peer_id)
##     print(InterestPolicy.explain(k, viewers, peer_id))
## [/codeblock]
class InterestPolicy:
	extends RefCounted

	## Returns the per-peer visibility verdict.
	##
	## [param kind] is one of [enum NetwInterestLayer.Policy].
	## [param viewers] is the layer's viewer set. Server peer
	## ([constant MultiplayerPeer.TARGET_PEER_SERVER]) is always admitted;
	## peer id [code]0[/code] is always rejected.
	static func verdict(
			kind: NetwInterestLayer.Policy,
			viewers: Dictionary,
			peer_id: int,
	) -> bool:
		if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
			return true
		if peer_id == 0:
			return false
		match kind:
			NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
				return viewers.has(peer_id)
			NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
				return not viewers.has(peer_id)
		return true


	## Returns a one-line description of why [param peer_id] resolved the
	## way it did. Intended for log lines and debugger inspection.
	static func explain(
			kind: NetwInterestLayer.Policy,
			viewers: Dictionary,
			peer_id: int,
	) -> String:
		if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
			return "ADMIT peer=SERVER (always admitted)"
		if peer_id == 0:
			return "REJECT peer=0 (no peer context)"
		var in_viewers := viewers.has(peer_id)
		var label := _kind_label(kind)
		match kind:
			NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
				if in_viewers:
					return "ADMIT peer=%d in viewers under %s" \
							% [peer_id, label]
				return "REJECT peer=%d not in viewers under %s" \
						% [peer_id, label]
			NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
				if in_viewers:
					return "REJECT peer=%d in viewers under %s" \
							% [peer_id, label]
				return "ADMIT peer=%d not in viewers under %s" \
						% [peer_id, label]
		return "ADMIT peer=%d (unknown kind=%d defaults true)" \
				% [peer_id, kind]


	static func _kind_label(kind: NetwInterestLayer.Policy) -> String:
		match kind:
			NetwInterestLayer.Policy.HIDE_FROM_OUTSIDERS:
				return "HIDE_FROM_OUTSIDERS"
			NetwInterestLayer.Policy.HIDE_FROM_INSIDERS:
				return "HIDE_FROM_INSIDERS"
		return "kind=%d" % kind
