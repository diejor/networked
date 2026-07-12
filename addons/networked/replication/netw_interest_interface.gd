## Applies [NetwInterestLayer] state to Godot replication.
##
## One lives under each [MultiplayerTree], owned by [NetwMultiplayer] and
## exposed at [member NetwMultiplayer.interest]. Layers are pure state.
## This interface installs entity visibility filters, drives server-side
## transition signals, updates bound [InterestGate] snapshots, and relays
## optional owner-side observer events. [NetwInterestLayer] owns mutation,
## policy, and transition signals.
##
## [br][br]
## Client-side [signal NetwInterestLayer.entity_visible] /
## [signal NetwInterestLayer.entity_hidden] are delivered by different
## transports depending on the layer:
## [br]- A bound layer's [InterestGate] admits local entities as they appear
## under the gated subtree.
## [br]- An unbound layer's transitions are relayed by the server over the
## network.
## Relay and entity spawn can race during same-tick admit storms. A bounded
## retry reconciles them.
##
## [br][br]
## Unbound layers do not replicate layer state. They affect the wire only
## by changing each entity [MultiplayerSynchronizer]'s visibility. Bound
## layers also replicate [member NetwInterestLayer.viewers] and
## [member NetwInterestLayer.policy] through their gate.
##
## [br][br]
## Scene gates are parent visibility. Generic layers should refine
## visibility under an already-admitted scene, not reveal scene roots by
## themselves.
##
## [br][br]
## Server code usually creates layers through [method layer]. Client
## code should only rely on layers mirrored by an [InterestGate], or on
## observer signals relayed by [member InterestComponent.report_observers].
## [codeblock]
## var arena := Netw.of(self).interest.layer(&"arena")
## arena.add_entity(player_entity)
## arena.add_viewer(player.peer_id)
## [/codeblock]
class_name NetwInterestInterface
extends RefCounted

var _layers: Dictionary[StringName, NetwInterestLayer] = { }
var _gates: Dictionary[StringName, InterestGate] = { }
var _entity_layers: Dictionary[NetwEntity, Dictionary] = { }
var _entity_filters: Dictionary[NetwEntity, Callable] = { }
var _entity_exit_handlers: Dictionary[NetwEntity, Callable] = { }
# Per-entity, per-peer count of layers currently admitting the peer.
# Visibility filter reads this in O(1); maintained by the layer
# interest_enter / interest_exit signal handlers.
var _admit_count: Dictionary[NetwEntity, Dictionary] = { }
var _dirty_entities: Dictionary[NetwEntity, bool] = { }
var _dirty_gate_layers: Dictionary[StringName, bool] = { }
var _refresh_scheduled: bool = false

## Transition kind for relayed visibility / observer events.
enum Kind { EXIT, ENTER }


# Server-side queue payload for a peer's local-view transition.
class _VisRelay:
	extends RefCounted
	var route: int
	var layer_id: StringName
	var kind: int


	func _init(r: int, l: StringName, k: int) -> void:
		route = r
		layer_id = l
		kind = k


	func to_wire() -> Array:
		return [route, layer_id, kind]


	static func from_wire(raw: Variant) -> _VisRelay:
		if typeof(raw) != TYPE_ARRAY or (raw as Array).size() != 3:
			return null
		return _VisRelay.new(raw[0], raw[1], raw[2])


# Server-side queue payload for an owner-side observer transition.
class _ObsRelay:
	extends RefCounted
	var path: NodePath
	var layer_id: StringName
	var observer_peer: int
	var kind: int


	func _init(
			p: NodePath,
			l: StringName,
			o: int,
			k: int,
	) -> void:
		path = p
		layer_id = l
		observer_peer = o
		kind = k


	func to_wire() -> Array:
		return [path, layer_id, observer_peer, kind]


	static func from_wire(raw: Variant) -> _ObsRelay:
		if typeof(raw) != TYPE_ARRAY or (raw as Array).size() != 4:
			return null
		return _ObsRelay.new(raw[0], raw[1], raw[2], raw[3])


var _observer_relay: Dictionary[int, Array] = { }
var _visibility_relay: Dictionary[int, Array] = { }

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	var mt := api.tree if api else null
	if mt:
		mt.peer_disconnected.connect(_on_peer_disconnected)
		mt.session_ended.connect(_on_session_ended)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


func _tree() -> MultiplayerTree:
	var api := _api()
	return api.tree if api else null


func _on_peer_disconnected(peer_id: int) -> void:
	_visibility_relay.erase(peer_id)
	_observer_relay.erase(peer_id)


# Defers the reset so it runs after any scene despawn driven by the same
# session_ended emission has drained entity state through the normal
# tree_exiting path. Clearing the layers and admit counters mid-despawn would
# desync them and trip the underflow assert. Deferring makes the reset
# independent of the order [SceneManager] and this interface handle the signal.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Drops every per-session entry so a same-layer second session starts from clean
# viewer and policy state. Bound gates self-unregister through their own
# _exit_tree when their scenes despawn, so this only clears the residue.
func _clear_session_state() -> void:
	_layers.clear()
	_gates.clear()
	_entity_layers.clear()
	_entity_filters.clear()
	_entity_exit_handlers.clear()
	_admit_count.clear()
	_dirty_entities.clear()
	_dirty_gate_layers.clear()
	_observer_relay.clear()
	_visibility_relay.clear()
	_refresh_scheduled = false


## Returns the layer for [param layer_id], creating it on first use.
func layer(layer_id: StringName) -> NetwInterestLayer:
	return layer_for(layer_id)


## Returns the layer for [param layer_id], creating it when missing.
func layer_for(layer_id: StringName) -> NetwInterestLayer:
	if layer_id.is_empty():
		return null
	var found: NetwInterestLayer = _layers.get(layer_id)
	if found:
		return found
	found = NetwInterestLayer.new(layer_id, self)
	_layers[layer_id] = found
	found.interest_enter.connect(_on_layer_interest_enter.bind(found))
	found.interest_exit.connect(_on_layer_interest_exit.bind(found))
	return found


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	return _layers.get(layer_id)


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var out: Array[NetwInterestLayer] = []
	out.assign(_layers.values())
	return out


## Returns tree-wide interest occupancy counters for [InterestMonitor].
##
## [code]visible_edges[/code] is the count of admitted (entity, peer) pairs across
## every layer, the matrix occupancy that drives replication bandwidth.
## [code]transitions_total[/code] sums the per-layer
## [method NetwInterestLayer.monitor_snapshot] churn since creation, so the monitor
## reads it as a delta over an interval.
## [codeblock]
## {
##   ┠╴ layers: int             # active layers
##   ┠╴ entities_filtered: int  # entities with a visibility filter installed
##   ┠╴ visible_edges: int      # admitted (entity, peer) pairs, all layers
##   ┠╴ dirty_entities: int     # entities pending a visibility recompute
##   ┠╴ relay_backlog: int      # relayed transitions awaiting entity liveness
##   ┖╴ transitions_total: int  # summed per-layer churn since creation
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	var visible_edges := 0
	for entity: NetwEntity in _admit_count:
		visible_edges += (_admit_count[entity] as Dictionary).size()
	var transitions_total := 0
	for layer_id: StringName in _layers:
		transitions_total += int(
			_layers[layer_id].monitor_snapshot()[&"transitions_total"],
		)
	return {
		&"layers": _layers.size(),
		&"entities_filtered": _entity_filters.size(),
		&"visible_edges": visible_edges,
		&"dirty_entities": _dirty_entities.size(),
		&"relay_backlog": _liveness_backlog(),
		&"transitions_total": transitions_total,
	}


## Returns the per-peer admit counts for [param entity].
func committed_admits(entity: NetwEntity) -> Dictionary:
	return _admit_count.get(entity, {})


## Returns [code]true[/code] if [param entity] has a visibility filter installed.
func has_filter(entity: NetwEntity) -> bool:
	return _entity_filters.has(entity)


## Returns [code]true[/code] if any layer admits [param peer_id] to
## [param entity].
func can_peer_see_entity(peer_id: int, entity: NetwEntity) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == 0 or entity == null:
		return false
	if not _is_server():
		return _current_layer_verdict(peer_id, entity)
	if not _ancestors_admit(peer_id, entity):
		return false
	var per_peer: Dictionary = _admit_count.get(entity, { })
	if per_peer.get(peer_id, 0) > 0:
		return true
	if not _dirty_entities.has(entity):
		return false
	return _current_layer_verdict(peer_id, entity)


func _on_layer_policy_changed(changed_layer: NetwInterestLayer) -> void:
	_mark_layer_dirty(changed_layer)
	if _is_server():
		_drive_layer(changed_layer)
	if _gates.has(changed_layer.layer_id):
		_mark_gate_dirty(changed_layer.layer_id)


func _on_layer_viewer_changed(
		changed_layer: NetwInterestLayer,
		peer_id: int,
		added: bool,
) -> void:
	if _gates.has(changed_layer.layer_id):
		_mark_gate_dirty(changed_layer.layer_id)
	_mark_layer_dirty(changed_layer)
	if _is_server():
		_drive_layer(changed_layer)


func _on_layer_entity_changed(
		changed_layer: NetwInterestLayer,
		entity: NetwEntity,
		added: bool,
) -> void:
	if added:
		_track_entity_layer(entity, changed_layer.layer_id)
		_install_entity_filter(entity)
	else:
		_untrack_entity_layer(entity, changed_layer.layer_id)
	_mark_entity_dirty(entity)
	if added and _is_server():
		_drive_layer(changed_layer)


## Registers [param gate] as the network carrier for its layer.
##
## [param gate] must be valid and configured with a non-empty
## [member InterestGate.layer_id].
func register_gate(gate: InterestGate) -> void:
	assert(
		is_instance_valid(gate),
		"NetwInterestInterface.register_gate: gate is freed",
	)
	assert(
		not gate.layer_id.is_empty(),
		"NetwInterestInterface.register_gate: gate.layer_id is empty",
	)
	var existing: InterestGate = _gates.get(gate.layer_id)
	if is_instance_valid(existing) and existing != gate:
		push_error(
			"NetwInterestInterface: gate already registered for layer '%s'"
			% [String(gate.layer_id)],
		)
		return
	_gates[gate.layer_id] = gate
	var gate_layer := get_layer(gate.layer_id)
	if gate_layer and _is_server():
		gate.apply_snapshot(gate_layer.viewers_packed(), gate_layer.policy)


## Removes [param gate] from the registry. Idempotent.
func unregister_gate(gate: InterestGate) -> void:
	if not is_instance_valid(gate):
		return
	if _gates.get(gate.layer_id) == gate:
		_gates.erase(gate.layer_id)


## Returns the gate bound to [param layer_id], or [code]null[/code].
func gate_for(layer_id: StringName) -> InterestGate:
	return _gates.get(layer_id)


func _track_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	var layers: Dictionary = _entity_layers.get_or_add(entity, { })
	layers[layer_id] = true


func _untrack_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	var layers: Dictionary = _entity_layers.get(entity, { })
	layers.erase(layer_id)
	if layers.is_empty():
		_entity_layers.erase(entity)


func _install_entity_filter(entity: NetwEntity) -> void:
	# Reached only via layer.add_entity, which asserts entity/owner.
	if _entity_filters.has(entity):
		return
	var filter := func(peer_id: int) -> bool:
		return can_peer_see_entity(peer_id, entity)
	_entity_filters[entity] = filter
	for sync in entity.synchronizers():
		if is_instance_valid(sync):
			sync.add_visibility_filter(filter)
	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)


func _uninstall_entity_filter(entity: NetwEntity) -> void:
	var filter: Callable = _entity_filters.get(entity, Callable())
	if filter.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner):
		for sync in entity.synchronizers():
			if is_instance_valid(sync):
				sync.remove_visibility_filter(filter)
	_entity_filters.erase(entity)
	var handler: Callable = _entity_exit_handlers.get(entity, Callable())
	if handler.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner) \
			and entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.disconnect(handler)
	_entity_exit_handlers.erase(entity)


func _on_entity_tree_exiting(entity: NetwEntity) -> void:
	var layer_ids: Dictionary = _entity_layers.get(entity, { }).duplicate()
	for layer_id: StringName in layer_ids:
		var exiting_layer := get_layer(layer_id)
		if not exiting_layer:
			continue
		if _is_server():
			exiting_layer.remove_entity(entity)
		else:
			exiting_layer._client_untrack_entity(entity)
	_uninstall_entity_filter(entity)
	_dirty_entities.erase(entity)
	assert(
		not _admit_count.has(entity),
		"NetwInterestInterface: admit_count leaked entries after layer removal",
	)
	_admit_count.erase(entity)


func _mark_layer_dirty(dirty_layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in dirty_layer._entities:
		_mark_entity_dirty(entity)


func _mark_entity_dirty(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestInterface: _mark_entity_dirty called with null entity",
	)
	_dirty_entities[entity] = true
	_schedule_visibility_flush()


func _mark_gate_dirty(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
	_dirty_gate_layers[layer_id] = true
	_mark_gate_descendants_dirty(layer_id)
	_schedule_visibility_flush()


func _mark_gate_descendants_dirty(layer_id: StringName) -> void:
	var gate: InterestGate = _gates.get(layer_id)
	if not is_instance_valid(gate):
		return
	var host := NetwEntity.of(gate)
	if host == null:
		return
	for entity: NetwEntity in _entity_filters.keys():
		if _has_ancestor_entity(entity, host):
			_dirty_entities[entity] = true


func _has_ancestor_entity(entity: NetwEntity, ancestor: NetwEntity) -> bool:
	var current := entity.parent_entity()
	while current != null:
		if current == ancestor:
			return true
		current = current.parent_entity()
	return false


func _schedule_visibility_flush() -> void:
	if _refresh_scheduled:
		return
	_refresh_scheduled = true
	_flush_visibility.call_deferred()


## Flushes gate snapshots, entity visibility, and observer events.
##
## Gate visibility runs in two passes around entity visibility so a
## losing-admit peer despawns nested entities before the wrapper they live
## under disappears.
## [codeblock]
## split:   gate admits -> entity despawns -> gate revokes
##          (nested children die before their wrapper)
## single:  the wrapper despawn cascade-frees the children first, then
##          every per-entity despawn fails ERR_UNAUTHORIZED
##          (no recv_nodes entry)
## [/codeblock]
func flush() -> void:
	var mt := _tree()
	if not is_instance_valid(mt) or mt.multiplayer_peer == null:
		return
	if mt.multiplayer_peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_DISCONNECTED:
		return
	_refresh_scheduled = false
	var transitions := _gather_gate_transitions()
	_apply_gate_admits(transitions)
	_drive_dirty_entity_layers()
	_flush_entity_visibility()
	_apply_gate_revokes(transitions)
	_flush_visibility_relay()
	_flush_observer_relay()
	# Admission changes drive per-peer spawn/despawn through the spawn book.
	var api := _api()
	if api:
		api.replication._spawn_pipeline.schedule_visibility_sweep()


## Flushes only bound gate snapshots.
##
## Use before spawning a subtree whose admission gate must be visible
## before child spawn packets are sent. Only additive admits are applied;
## any pending revokes are re-queued for the next [method flush] so they
## stay ordered after the nested entity despawns.
func flush_gates() -> void:
	var transitions := _gather_gate_transitions()
	_apply_gate_admits(transitions)
	_requeue_gate_revokes(transitions)


func _flush_visibility() -> void:
	flush()


# Writes new gate data and groups peers by current verdict.
# Returns {layer_id -> {"gate", "admits", "revokes"}}. Clears
# [code]_dirty_gate_layers[/code]; callers that need to defer revokes
# must re-queue them via [method _requeue_gate_revokes].
func _gather_gate_transitions() -> Dictionary:
	var out: Dictionary = { }
	var mt := _tree()
	if not is_instance_valid(mt) or mt.multiplayer_peer == null:
		_dirty_gate_layers.clear()
		return out
	if mt.multiplayer_peer.get_connection_status() \
			== MultiplayerPeer.CONNECTION_DISCONNECTED:
		_dirty_gate_layers.clear()
		return out
	var api := _api()
	if not api.is_server():
		_dirty_gate_layers.clear()
		return out
	var peers := api.get_peers()
	for layer_id: StringName in _dirty_gate_layers.keys():
		var gate: InterestGate = _gates.get(layer_id)
		var gate_layer := get_layer(layer_id)
		if not is_instance_valid(gate) or gate_layer == null:
			continue
		gate.apply_snapshot_data(gate_layer.viewers_packed(), gate_layer.policy)
		var admits: Array[int] = []
		var revokes: Array[int] = []
		for peer_id: int in peers:
			if gate.verdict_for(peer_id):
				admits.append(peer_id)
			else:
				revokes.append(peer_id)
		out[layer_id] = { "gate": gate, "admits": admits, "revokes": revokes }
	_dirty_gate_layers.clear()
	return out


func _apply_gate_admits(transitions: Dictionary) -> void:
	for layer_id: StringName in transitions:
		var info: Dictionary = transitions[layer_id]
		var gate: InterestGate = info["gate"]
		if is_instance_valid(gate):
			gate.apply_admission_visibility_to(info["admits"])


func _apply_gate_revokes(transitions: Dictionary) -> void:
	for layer_id: StringName in transitions:
		var info: Dictionary = transitions[layer_id]
		var gate: InterestGate = info["gate"]
		if is_instance_valid(gate):
			gate.apply_admission_visibility_to(info["revokes"])


func _requeue_gate_revokes(transitions: Dictionary) -> void:
	var requeued := false
	for layer_id: StringName in transitions:
		var info: Dictionary = transitions[layer_id]
		var revokes: Array = info["revokes"]
		if revokes.is_empty():
			continue
		_dirty_gate_layers[layer_id] = true
		requeued = true
	if requeued:
		_schedule_visibility_flush()


func _drive_dirty_entity_layers() -> void:
	if not _is_server():
		return
	var layer_ids: Dictionary[StringName, bool] = { }
	for entity: NetwEntity in _dirty_entities:
		if not is_instance_valid(entity.owner):
			continue
		if not entity.owner.is_inside_tree():
			continue
		var layers: Dictionary = _entity_layers.get(entity, { })
		for layer_id: StringName in layers:
			layer_ids[layer_id] = true
	for layer_id: StringName in layer_ids:
		_drive_layer(get_layer(layer_id))


func _flush_entity_visibility() -> void:
	# tree_exiting eviction normally keeps entries pointing at live owners, but a
	# bulk teardown (e.g. session end) can free an owner before its per-node
	# eviction runs, so skip a freed owner instead of asserting. Mirrors the same
	# guard in _drive_dirty_entity_layers.
	var still_dirty: Dictionary[NetwEntity, bool] = { }
	for entity: NetwEntity in _dirty_entities.keys():
		if not is_instance_valid(entity.owner):
			continue
		if not entity.owner.is_inside_tree():
			still_dirty[entity] = true
			continue
		for sync in entity.synchronizers():
			if is_instance_valid(sync) and sync.is_inside_tree():
				sync.update_visibility()
	_dirty_entities = still_dirty


func _drive_layer(layer_to_drive: NetwInterestLayer) -> void:
	if layer_to_drive == null:
		return
	layer_to_drive.drive_now(_live_peers(layer_to_drive))


func _current_layer_verdict(peer_id: int, entity: NetwEntity) -> bool:
	if not _ancestors_admit(peer_id, entity):
		return false
	var layer_ids: Dictionary = _entity_layers.get(entity, { })
	for layer_id: StringName in layer_ids:
		var verdict_layer := get_layer(layer_id)
		if verdict_layer and verdict_layer.has_entity(entity) \
				and verdict_layer.verdict_for(peer_id):
			return true
	return false


func _ancestors_admit(peer_id: int, entity: NetwEntity) -> bool:
	var current := entity.parent_entity()
	while current != null:
		var gate := current.interest_gate
		if is_instance_valid(gate) and not gate.verdict_for(peer_id):
			return false
		current = current.parent_entity()
	return true


func _live_peers(peers_layer: NetwInterestLayer) -> Array[int]:
	var seen: Dictionary[int, bool] = { }
	var mt := _tree()
	if is_instance_valid(mt) and mt.multiplayer_peer != null:
		var api := _api()
		if mt.is_server:
			for p in api.get_peers():
				seen[p] = true
		else:
			seen[api.get_unique_id()] = true
	for p in peers_layer.viewers:
		seen[p] = true
	for p in peers_layer.driver.cached_peers():
		seen[p] = true
	var out: Array[int] = []
	out.assign(seen.keys())
	return out


# Relayed transitions parked in NetwLivenessInterface.when_live for the monitor.
func _liveness_backlog() -> int:
	var api := _api()
	var liveness := api.liveness if api else null
	return liveness.pending_live_count() if liveness else 0


# Authority comes from the API, never the tree. The API answers server offline
# and in a disconnected window, so a unit rig without a peer reads as server.
func _is_server() -> bool:
	var api := _api()
	return api == null or api.is_server()


func _on_layer_interest_enter(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	var per_peer: Dictionary = _admit_count.get_or_add(entity, { })
	per_peer[peer_id] = int(per_peer.get(peer_id, 0)) + 1
	_queue_visibility_event(layer_source, entity, peer_id, Kind.ENTER)
	_queue_observer_event(layer_source, entity, peer_id, Kind.ENTER)


func _on_layer_interest_exit(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	var per_peer: Dictionary = _admit_count.get(entity, { })
	var next := int(per_peer.get(peer_id, 0)) - 1
	assert(
		next >= 0,
		"NetwInterestInterface: admit_count underflow for entity/peer",
	)
	if next <= 0:
		per_peer.erase(peer_id)
		if per_peer.is_empty():
			_admit_count.erase(entity)
	else:
		per_peer[peer_id] = next
	_queue_visibility_event(layer_source, entity, peer_id, Kind.EXIT)
	_queue_observer_event(layer_source, entity, peer_id, Kind.EXIT)


func _queue_visibility_event(
		event_layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	# Bound layers deliver client transitions through their gate's local
	# entity tracking. Only unbound layers use this carrier relay.
	if event_layer.bound_gate() != null:
		return
	assert(
		entity != null and is_instance_valid(entity.owner),
		"NetwInterestInterface: transition emitted for freed entity",
	)
	if observer_peer == 0 or observer_peer == MultiplayerPeer.TARGET_PEER_SERVER:
		return
	if not entity.owner.is_inside_tree():
		return
	var mt := _tree()
	if not _can_send_to_peer(mt, observer_peer):
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	# Server-side allocation is legal here, but the client only learns the
	# route through a spawn channel. An entity spawned outside that channel
	# and with no envelope route needs a manual bind_route on every peer, or
	# this event expires client-side.
	var route := liveness.allocate_route(entity)
	var bucket: Array = _visibility_relay.get_or_add(observer_peer, [])
	bucket.append(
		_VisRelay.new(
			route,
			event_layer.layer_id,
			kind,
		),
	)
	_schedule_visibility_flush()


func _flush_visibility_relay() -> void:
	if _visibility_relay.is_empty():
		return
	if not _is_server():
		_visibility_relay.clear()
		return
	for observer_peer: int in _visibility_relay.keys():
		var events: Array = _visibility_relay[observer_peer]
		if events.is_empty():
			continue
		var mt := _tree()
		if not _can_send_to_peer(mt, observer_peer):
			continue
		var wire: Array = []
		for e: _VisRelay in events:
			wire.append(e.to_wire())
		_send_events(
			observer_peer,
			NetwFrameEnvelope.Channel.INTEREST_VISIBILITY,
			wire,
		)
	_visibility_relay.clear()


func _queue_observer_event(
		event_layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	assert(
		entity != null and is_instance_valid(entity.owner),
		"NetwInterestInterface: transition emitted for freed entity",
	)
	if entity.peer_id == 0 or observer_peer == entity.peer_id:
		return
	if event_layer.bound_gate() != null:
		return
	var component := InterestComponent.of(entity)
	if not component or not component.report_observers:
		return
	if not entity.owner.is_inside_tree():
		return
	var mt := _tree()
	if not _can_send_to_peer(mt, entity.peer_id):
		return
	var owner_peer := entity.peer_id
	var bucket: Array = _observer_relay.get_or_add(owner_peer, [])
	bucket.append(
		_ObsRelay.new(
			mt.get_path_to(entity.owner),
			event_layer.layer_id,
			observer_peer,
			kind,
		),
	)
	_schedule_visibility_flush()


func _flush_observer_relay() -> void:
	if _observer_relay.is_empty():
		return
	if not _is_server():
		_observer_relay.clear()
		return
	for owner_peer: int in _observer_relay.keys():
		var events: Array = _observer_relay[owner_peer]
		if events.is_empty():
			continue
		var mt := _tree()
		if not _can_send_to_peer(mt, owner_peer):
			continue
		var wire: Array = []
		for e: _ObsRelay in events:
			wire.append(e.to_wire())
		_send_events(
			owner_peer,
			NetwFrameEnvelope.Channel.INTEREST_OBSERVER,
			wire,
		)
	_observer_relay.clear()


func _can_send_to_peer(mt: MultiplayerTree, peer_id: int) -> bool:
	if not is_instance_valid(mt):
		return false
	if peer_id == 0 or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return false
	var api := _api()
	if not api or not api.has_multiplayer_peer():
		return false
	var peer := mt.multiplayer_peer
	if peer == null or peer is OfflineMultiplayerPeer:
		return false
	if peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED:
		return false
	if api.is_server():
		return peer_id in api.get_peers()
	return peer_id == MultiplayerPeer.TARGET_PEER_SERVER


# Sends one relay event batch to peer_id as a reliable route-0 carrier frame.
func _send_events(peer_id: int, channel: NetwFrameEnvelope.Channel, wire: Array) -> void:
	var api := _api()
	if not api:
		return
	api.replication.send_to(
		peer_id,
		0,
		channel,
		var_to_bytes(wire),
		true,
	)


# Carrier receive for Channel.INTEREST_OBSERVER frames, dispatched by
# NetwReplicationInterface. Only server-authored events are honored.
func _handle_observer_events(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var events = bytes_to_var(payload)
	if typeof(events) != TYPE_ARRAY:
		return
	var mt := _tree()
	if not is_instance_valid(mt):
		return
	for raw in events:
		var event := _ObsRelay.from_wire(raw)
		if event == null:
			continue
		var node := mt.get_node_or_null(event.path)
		if not is_instance_valid(node):
			continue
		var entity := NetwEntity.of(node)
		if not entity:
			continue
		if event.kind == Kind.ENTER:
			entity.observer_entered.emit(event.layer_id, event.observer_peer)
		else:
			entity.observer_left.emit(event.layer_id, event.observer_peer)


# Carrier receive for Channel.INTEREST_VISIBILITY frames, dispatched by
# NetwReplicationInterface. Only server-authored events are honored.
func _handle_visibility_events(payload: PackedByteArray, sender: int) -> void:
	if sender != 1:
		return
	var events = bytes_to_var(payload)
	if typeof(events) != TYPE_ARRAY:
		return
	var mt := _tree()
	if not is_instance_valid(mt):
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	for raw in events:
		var event := _VisRelay.from_wire(raw)
		if event == null:
			continue
		# An EXIT for an entity that never became live is a no-op, so only
		# ENTER events park and wait for the spawn window.
		if event.kind != Kind.ENTER \
				and liveness.route_state(event.route) \
				!= NetwLivenessInterface.State.LIVE:
			continue
		liveness.when_live(event.route, func():
			_apply_visibility_event(mt, event)
		)


func _apply_visibility_event(mt: MultiplayerTree, event: _VisRelay) -> void:
	if not is_instance_valid(mt):
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	var entity := liveness.entity_of(event.route)
	if not entity:
		return
	var event_layer := layer_for(event.layer_id)
	if not event_layer:
		return
	if event.kind == Kind.ENTER:
		event_layer._client_admit(entity)
	else:
		event_layer._client_revoke(entity)
