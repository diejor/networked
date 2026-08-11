## Server-owned membership and per-peer visibility for one interest slice.
##
## A layer combines [member entities], [member viewers], and [member policy].
## [member default_leave_policy] governs wire removal, while
## [member default_perception_policy] governs local presentation. Entity
## membership never crosses the wire.
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
## var sight := Netw.of(self).interest.layer(&"sight")
## sight.entity_visible.connect(func(entity):
##     add_marker(entity.owner)
## )
##
## # Owner client: react to who can see this entity.
## NetwEntity.of(self).observer_entered.connect(func(layer_id, peer_id):
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

## Stable id used by [InterestCore] to index this layer.
var layer_id: StringName

## Composition policy. See [enum Policy].
var policy: Policy = Policy.HIDE_FROM_OUTSIDERS

## Wire behavior when this layer stops admitting an entity and the entity has
## no [method NetwInterestHandle.on_leave_policy] override.
var default_leave_policy: NetwMultiplayer.LeavePolicy = \
		NetwMultiplayer.LeavePolicy.DESPAWN

## Local presentation behavior when this layer stops admitting an entity and
## no per-entity override is configured.
var default_perception_policy: NetwMultiplayer.PerceptionPolicy = \
		NetwMultiplayer.PerceptionPolicy.HIDE:
	set(value):
		# Range check, so the enum's ordering is contract. Renumbering
		# PerceptionPolicy so HIDE and CUSTOM stop bounding it breaks this.
		assert(
			value >= NetwMultiplayer.PerceptionPolicy.HIDE
			and value <= NetwMultiplayer.PerceptionPolicy.CUSTOM,
			"NetwInterestLayer: invalid default_perception_policy",
		)
		if default_perception_policy == value:
			return
		default_perception_policy = value
		var s := _service()
		if s:
			s._on_layer_perception_policy_changed(self)

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

var _service_ref: WeakRef
# Cumulative show plus hide transitions emitted by this layer, read as a delta by
# the debug monitor to surface per-layer visibility churn.
var _transition_count: int = 0


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


## Removes [param entity] from this layer. Server authoritative. Returns
## [code]false[/code] on clients. Visibility exits are emitted at the next
## [method InterestCore.flush].
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
	_entities.erase(entity)
	entity_removed.emit(entity)
	if s:
		s._on_layer_entity_changed(self, entity, false)
	return true


## Returns [code]true[/code] when [param entity] is in this layer.
func has_entity(entity: NetwEntity) -> bool:
	return _entities.has(entity)


# Idempotent client-side membership path that also updates interface tracking.
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
	entity.interest._client_join_label(layer_id)
	entity.interest._dispatch_enter(layer_id, _local_peer_id())
	entity_visible.emit(entity)
	if s:
		s._refresh_local_perception(entity, [layer_id])


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
	var s := _service()
	if s:
		s._on_layer_entity_changed(self, entity, false)
	entity.interest._dispatch_leave(layer_id, _local_peer_id())
	entity_hidden.emit(entity)
	if s:
		s._refresh_local_perception(entity, [layer_id])


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
	entity.interest._client_join_label(layer_id)
	entity.interest._dispatch_enter(layer_id, _local_peer_id())
	entity_visible.emit(entity)
	var s := _service()
	if s:
		s._refresh_local_perception(entity, [layer_id])


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
	entity.interest._dispatch_leave(layer_id, _local_peer_id())
	entity_hidden.emit(entity)
	var s := _service()
	if s:
		s._refresh_local_perception(entity, [layer_id])


## Returns the committed verdict for [param entity] and [param peer_id].
func is_visible_to(entity: NetwEntity, peer_id: int) -> bool:
	var service := _service()
	if service:
		return service.participant_sees(peer_id, entity)
	return has_entity(entity) and verdict_for(peer_id)


## Returns the current policy verdict for [param peer_id].
func verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, viewers, peer_id)


## Returns aggregate occupancy counters for [InterestMonitor].
##
## [code]transitions_total[/code] is cumulative since this layer was created, so
## the monitor reads it as a delta over an interval to surface churn.
## [code]visible_edges[/code] is the count of admitted entity and peer pairs in
## the committed engine matrix.
## [codeblock]
## {
##   ┠╴ viewers: int            # size of viewers
##   ┠╴ entities: int           # size of entities
##   ┠╴ visible_edges: int      # admitted (entity, peer) pairs
##   ┖╴ transitions_total: int  # summed show plus hide since creation
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	var service := _service()
	return {
		&"viewers": viewers.size(),
		&"entities": _entities.size(),
		&"visible_edges": service.layer_visible_edges(layer_id) if service else 0,
		&"transitions_total": _transition_count,
	}


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
	}


## Returns current viewer peer ids.
func viewer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(viewers.keys())
	return out


func _apply_server_transition(
		entity: NetwEntity,
		peer_id: int,
		visible: bool,
) -> void:
	_transition_count += 1
	if visible:
		interest_enter.emit(entity, peer_id)
		entity.interest_enter.emit(peer_id)
		entity.interest._dispatch_enter(layer_id, peer_id)
	else:
		interest_exit.emit(entity, peer_id)
		entity.interest_exit.emit(peer_id)
		entity.interest._dispatch_leave(layer_id, peer_id)


func _service() -> InterestCore:
	return _service_ref.get_ref() as InterestCore if _service_ref else null


# Returns the participant id used by client-side handle callbacks.
func _local_peer_id() -> int:
	var service := _service()
	var api := service._api() if service else null
	return api.get_unique_id() if api and api.has_multiplayer_peer() else 1


## Stateless verdict resolver for [NetwInterestLayer].
##
## Given a [enum NetwInterestLayer.Policy] and a viewer set, returns the
## per-peer layer verdict. The committed [NetwInterestEngine] matrix composes these
## verdicts across layer membership and entity ancestry.
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
	## [param viewers] is the layer's viewer set. Peer id [code]0[/code] is
	## always rejected. The listen host is evaluated like every participant.
	static func verdict(
			kind: NetwInterestLayer.Policy,
			viewers: Dictionary,
			peer_id: int,
	) -> bool:
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
