## Applies [NetwInterestLayer] state to Godot replication.
##
## One service lives under each [MultiplayerTree]. Layers are pure state;
## this service installs entity visibility filters, drives server-side
## transition signals, updates bound [InterestGate] snapshots, and relays
## optional owner-side observer events.
##
## [br][br]
## Client-side [signal NetwInterestLayer.entity_visible] /
## [signal NetwInterestLayer.entity_hidden] are delivered by different
## transports depending on the layer:
## [br]- Bound layers: [InterestGate] admits local entities as they appear
## under the gated subtree.
## [br]- Unbound layers: the server relays transitions over the network.
## Relay and entity spawn can race during same-tick admit storms; a bounded
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
class_name InterestService
extends Node

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
	var path: NodePath
	var layer_id: StringName
	var kind: int


	func _init(p: NodePath, l: StringName, k: int) -> void:
		path = p
		layer_id = l
		kind = k


	func to_wire() -> Array:
		return [path, layer_id, kind]


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
var _pending_visibility_events: Array[_VisRelay] = []
var _pending_attempts: Array[int] = []
var _pending_visibility_flush_scheduled: bool = false


func _enter_tree() -> void:
	NetwServices.register(self, InterestService)
	var mt := _tree()
	if is_instance_valid(mt):
		mt.peer_disconnected.connect(_on_peer_disconnected)
		mt.session_ended.connect(_on_session_ended)


func _exit_tree() -> void:
	var mt := _tree()
	if is_instance_valid(mt):
		if mt.peer_disconnected.is_connected(_on_peer_disconnected):
			mt.peer_disconnected.disconnect(_on_peer_disconnected)
		if mt.session_ended.is_connected(_on_session_ended):
			mt.session_ended.disconnect(_on_session_ended)
	NetwServices.unregister(self, InterestService)


func _on_peer_disconnected(peer_id: int) -> void:
	_visibility_relay.erase(peer_id)
	_observer_relay.erase(peer_id)


# Defers the reset so it runs after any scene despawn driven by the same
# session_ended emission has drained entity state through the normal
# tree_exiting path. Clearing the layers and admit counters mid-despawn would
# desync them and trip the underflow assert. Deferring makes the reset
# independent of the order [SceneManager] and this service handle the signal.
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
	_pending_visibility_events.clear()
	_pending_attempts.clear()
	_refresh_scheduled = false
	_pending_visibility_flush_scheduled = false


## Returns the layer for [param layer_id], creating it when missing.
func layer_for(layer_id: StringName) -> NetwInterestLayer:
	if layer_id.is_empty():
		return null
	var layer: NetwInterestLayer = _layers.get(layer_id)
	if layer:
		return layer
	layer = NetwInterestLayer.new(layer_id, self)
	_layers[layer_id] = layer
	layer.interest_enter.connect(_on_layer_interest_enter.bind(layer))
	layer.interest_exit.connect(_on_layer_interest_exit.bind(layer))
	return layer


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	return _layers.get(layer_id)


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var out: Array[NetwInterestLayer] = []
	out.assign(_layers.values())
	return out


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


func _on_layer_policy_changed(layer: NetwInterestLayer) -> void:
	_mark_layer_dirty(layer)
	if _is_server():
		_drive_layer(layer)
	if _gates.has(layer.layer_id):
		_mark_gate_dirty(layer.layer_id)


func _on_layer_viewer_changed(
		layer: NetwInterestLayer,
		peer_id: int,
		added: bool,
) -> void:
	if _gates.has(layer.layer_id):
		_mark_gate_dirty(layer.layer_id)
	_mark_layer_dirty(layer)
	if _is_server():
		_drive_layer(layer)


func _on_layer_entity_changed(
		layer: NetwInterestLayer,
		entity: NetwEntity,
		added: bool,
) -> void:
	if added:
		_track_entity_layer(entity, layer.layer_id)
		_install_entity_filter(entity)
	else:
		_untrack_entity_layer(entity, layer.layer_id)
	_mark_entity_dirty(entity)
	if added and _is_server():
		_drive_layer(layer)


## Registers [param gate] as the network carrier for its layer.
##
## [param gate] must be valid and configured with a non-empty
## [member InterestGate.layer_id].
func register_gate(gate: InterestGate) -> void:
	assert(
		is_instance_valid(gate),
		"InterestService.register_gate: gate is freed",
	)
	assert(
		not gate.layer_id.is_empty(),
		"InterestService.register_gate: gate.layer_id is empty",
	)
	var existing: InterestGate = _gates.get(gate.layer_id)
	if is_instance_valid(existing) and existing != gate:
		push_error(
			"InterestService: gate already registered for layer '%s'"
			% [String(gate.layer_id)],
		)
		return
	_gates[gate.layer_id] = gate
	var layer := get_layer(gate.layer_id)
	if layer and _is_server():
		gate.apply_snapshot(layer.viewers_packed(), layer.policy)


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
		var layer := get_layer(layer_id)
		if not layer:
			continue
		if _is_server():
			layer.remove_entity(entity)
		else:
			layer._client_untrack_entity(entity)
	_uninstall_entity_filter(entity)
	_dirty_entities.erase(entity)
	assert(
		not _admit_count.has(entity),
		"InterestService: admit_count leaked entries after layer removal",
	)
	_admit_count.erase(entity)


func _mark_layer_dirty(layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in layer._entities:
		_mark_entity_dirty(entity)


func _mark_entity_dirty(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"InterestService: _mark_entity_dirty called with null entity",
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
## Gate visibility runs in two passes around entity visibility so that
## losing-admit peers despawn nested entities before the wrapper they
## live under disappears. Without the split, the wrapper despawn would
## arrive first on the client and cascade-free nested children, leaving
## the server's subsequent per-entity despawn packets to fail with
## [code]ERR_UNAUTHORIZED[/code] (no [code]recv_nodes[/code] entry).
func flush() -> void:
	_refresh_scheduled = false
	var transitions := _gather_gate_transitions()
	_apply_gate_admits(transitions)
	_drive_dirty_entity_layers()
	_flush_entity_visibility()
	_apply_gate_revokes(transitions)
	_flush_visibility_relay()
	_flush_observer_relay()


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
	if not is_instance_valid(mt) or mt.multiplayer_peer == null \
			or not mt.multiplayer_api.is_server():
		_dirty_gate_layers.clear()
		return out
	var peers := mt.multiplayer_api.get_peers()
	for layer_id: StringName in _dirty_gate_layers.keys():
		var gate: InterestGate = _gates.get(layer_id)
		var layer := get_layer(layer_id)
		if not is_instance_valid(gate) or layer == null:
			continue
		gate.apply_snapshot_data(layer.viewers_packed(), layer.policy)
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
	# tree_exiting eviction guarantees entries refer to live owners.
	var still_dirty: Dictionary[NetwEntity, bool] = { }
	for entity: NetwEntity in _dirty_entities.keys():
		assert(
			is_instance_valid(entity.owner),
			"InterestService: dirty entity outlived its owner",
		)
		if not entity.owner.is_inside_tree():
			still_dirty[entity] = true
			continue
		for sync in entity.synchronizers():
			if is_instance_valid(sync) and sync.is_inside_tree():
				sync.update_visibility()
	_dirty_entities = still_dirty


func _drive_layer(layer: NetwInterestLayer) -> void:
	if layer == null:
		return
	layer.drive_now(_live_peers(layer))


func _current_layer_verdict(peer_id: int, entity: NetwEntity) -> bool:
	if not _ancestors_admit(peer_id, entity):
		return false
	var layer_ids: Dictionary = _entity_layers.get(entity, { })
	for layer_id: StringName in layer_ids:
		var layer := get_layer(layer_id)
		if layer and layer.has_entity(entity) and layer.verdict_for(peer_id):
			return true
	return false


func _ancestors_admit(peer_id: int, entity: NetwEntity) -> bool:
	var current := entity.parent_entity()
	while current != null:
		var gate := current.slot(NetwEntity.Slot.INTEREST_GATE) as InterestGate
		if is_instance_valid(gate) and not gate.verdict_for(peer_id):
			return false
		current = current.parent_entity()
	return true


func _live_peers(layer: NetwInterestLayer) -> Array[int]:
	var seen: Dictionary[int, bool] = { }
	var mt := _tree()
	if is_instance_valid(mt) and mt.multiplayer_peer != null:
		if mt.is_server:
			for p in mt.multiplayer_api.get_peers():
				seen[p] = true
		else:
			seen[mt.multiplayer_api.get_unique_id()] = true
	for p in layer.viewers:
		seen[p] = true
	for p in layer.driver.cached_peers():
		seen[p] = true
	var out: Array[int] = []
	out.assign(seen.keys())
	return out


func _tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)


func _is_server() -> bool:
	var mt := _tree()
	if not is_instance_valid(mt):
		return true
	if not mt.multiplayer_api or mt.multiplayer_peer == null:
		return true
	return mt.multiplayer_api.is_server()


func _on_layer_interest_enter(
		entity: NetwEntity,
		peer_id: int,
		layer: NetwInterestLayer,
) -> void:
	var per_peer: Dictionary = _admit_count.get_or_add(entity, { })
	per_peer[peer_id] = int(per_peer.get(peer_id, 0)) + 1
	_queue_visibility_event(layer, entity, peer_id, Kind.ENTER)
	_queue_observer_event(layer, entity, peer_id, Kind.ENTER)


func _on_layer_interest_exit(
		entity: NetwEntity,
		peer_id: int,
		layer: NetwInterestLayer,
) -> void:
	var per_peer: Dictionary = _admit_count.get(entity, { })
	var next := int(per_peer.get(peer_id, 0)) - 1
	assert(
		next >= 0,
		"InterestService: admit_count underflow for entity/peer",
	)
	if next <= 0:
		per_peer.erase(peer_id)
		if per_peer.is_empty():
			_admit_count.erase(entity)
	else:
		per_peer[peer_id] = next
	_queue_visibility_event(layer, entity, peer_id, Kind.EXIT)
	_queue_observer_event(layer, entity, peer_id, Kind.EXIT)


func _queue_visibility_event(
		layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	# Bound layers deliver client transitions through their gate's local
	# entity tracking. Only unbound layers use this RPC relay.
	if layer.bound_gate() != null:
		return
	assert(
		entity != null and is_instance_valid(entity.owner),
		"InterestService: transition emitted for freed entity",
	)
	if observer_peer == 0 or observer_peer == MultiplayerPeer.TARGET_PEER_SERVER:
		return
	if not entity.owner.is_inside_tree():
		return
	var mt := _tree()
	if not _can_send_rpc_to_peer(mt, observer_peer):
		return
	var bucket: Array = _visibility_relay.get_or_add(observer_peer, [])
	bucket.append(
		_VisRelay.new(
			mt.get_path_to(entity.owner),
			layer.layer_id,
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
		if not _can_send_rpc_to_peer(mt, observer_peer):
			continue
		var wire: Array = []
		for e: _VisRelay in events:
			wire.append(e.to_wire())
		_rpc_visibility_events.rpc_id(observer_peer, wire)
	_visibility_relay.clear()


func _queue_observer_event(
		layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	assert(
		entity != null and is_instance_valid(entity.owner),
		"InterestService: transition emitted for freed entity",
	)
	if entity.peer_id == 0 or observer_peer == entity.peer_id:
		return
	if layer.bound_gate() != null:
		return
	var component := InterestComponent.of(entity)
	if not component or not component.report_observers:
		return
	if not entity.owner.is_inside_tree():
		return
	var mt := _tree()
	if not _can_send_rpc_to_peer(mt, entity.peer_id):
		return
	var owner_peer := entity.peer_id
	var bucket: Array = _observer_relay.get_or_add(owner_peer, [])
	bucket.append(
		_ObsRelay.new(
			mt.get_path_to(entity.owner),
			layer.layer_id,
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
		if not _can_send_rpc_to_peer(mt, owner_peer):
			continue
		var wire: Array = []
		for e: _ObsRelay in events:
			wire.append(e.to_wire())
		_rpc_observer_events.rpc_id(owner_peer, wire)
	_observer_relay.clear()


func _can_send_rpc_to_peer(mt: MultiplayerTree, peer_id: int) -> bool:
	if not is_instance_valid(mt):
		return false
	if peer_id == 0 or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return false
	if not mt.multiplayer_api or not mt.multiplayer_api.has_multiplayer_peer():
		return false
	var peer := mt.multiplayer_peer
	if peer == null or peer is OfflineMultiplayerPeer:
		return false
	if peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED:
		return false
	if mt.multiplayer_api.is_server():
		return peer_id in mt.multiplayer_api.get_peers()
	return peer_id == MultiplayerPeer.TARGET_PEER_SERVER


@rpc("authority", "call_remote", "reliable")
func _rpc_observer_events(events: Array) -> void:
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


@rpc("authority", "call_remote", "reliable")
func _rpc_visibility_events(events: Array) -> void:
	var mt := _tree()
	if not is_instance_valid(mt):
		return
	for raw in events:
		var event := _VisRelay.from_wire(raw)
		if event == null:
			continue
		if not _apply_visibility_event(mt, event):
			_pending_visibility_events.append(event)
			_pending_attempts.append(0)
	if not _pending_visibility_events.is_empty():
		_schedule_pending_visibility_flush()


func _apply_visibility_event(mt: MultiplayerTree, event: _VisRelay) -> bool:
	if not is_instance_valid(mt):
		return true
	var node := mt.get_node_or_null(event.path)
	if not is_instance_valid(node):
		return event.kind != Kind.ENTER
	var entity := NetwEntity.of(node)
	if not entity:
		return true
	var layer := layer_for(event.layer_id)
	if not layer:
		return true
	if event.kind == Kind.ENTER:
		layer._client_admit(entity)
	else:
		layer._client_revoke(entity)
	return true


func _flush_pending_visibility_events() -> void:
	_pending_visibility_flush_scheduled = false
	var pending_events := _pending_visibility_events
	var pending_attempts := _pending_attempts
	_pending_visibility_events = []
	_pending_attempts = []
	var mt := _tree()
	for i in pending_events.size():
		var event := pending_events[i]
		var attempts := pending_attempts[i]
		if _apply_visibility_event(mt, event):
			continue
		if attempts < 30:
			_pending_visibility_events.append(event)
			_pending_attempts.append(attempts + 1)
			continue
		Netw.dbg.warn(
			"InterestService: ENTER for missing node '%s'",
			[String(event.path)],
			func(m): push_warning(m)
		)
	if not _pending_visibility_events.is_empty():
		_schedule_pending_visibility_flush()


func _schedule_pending_visibility_flush() -> void:
	if _pending_visibility_flush_scheduled:
		return
	if not is_inside_tree():
		return
	_pending_visibility_flush_scheduled = true
	get_tree().process_frame.connect(
		_flush_pending_visibility_events,
		CONNECT_ONE_SHOT,
	)
