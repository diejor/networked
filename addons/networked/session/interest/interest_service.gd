## Applies [NetwInterestLayer] state to Godot replication.
##
## One service lives under each [MultiplayerTree]. Layers are pure state;
## this service installs entity visibility filters, drives transition
## signals, updates bound [InterestGate] snapshots, and relays optional
## observer events.
##
## [br][br]
## Unbound layers do not replicate layer state. They affect the wire only
## by changing each entity [MultiplayerSynchronizer]'s visibility. Bound
## layers also replicate [member NetwInterestLayer.viewers] and
## [member NetwInterestLayer.policy] through their gate.
##
## [br][br]
## Client-side [signal NetwInterestLayer.entity_visible] and
## [signal NetwInterestLayer.entity_hidden] are transition events,
## relayed from the server. They do not mean the client owns the layer's
## [member NetwInterestLayer.entities] set.
##
## [br][br]
## Scene gates are parent visibility. Generic layers should refine
## visibility under an already-admitted scene, not reveal scene roots by
## themselves.
class_name InterestService
extends Node


var _layers: Dictionary[StringName, NetwInterestLayer] = {}
var _gates: Dictionary[StringName, InterestGate] = {}
var _entity_layers: Dictionary[NetwEntity, Dictionary] = {}
var _entity_filters: Dictionary[NetwEntity, Callable] = {}
var _entity_exit_handlers: Dictionary[NetwEntity, Callable] = {}
var _dirty_entities: Dictionary[NetwEntity, bool] = {}
var _dirty_gate_layers: Dictionary[StringName, bool] = {}
var _refresh_scheduled: bool = false

# Queued observer events keyed by owning peer id.
# Event shape: [entity_path, layer_id, observer_peer, ObserverEvent].
enum ObserverEvent { EXIT, ENTER }
var _observer_relay: Dictionary[int, Array] = {}
var _visibility_relay: Dictionary[int, Array] = {}
var _pending_visibility_events: Array = []


func _enter_tree() -> void:
	NetwServices.register(self, InterestService)
	var mt := _tree()
	if is_instance_valid(mt):
		mt.peer_disconnected.connect(_on_peer_disconnected)


func _exit_tree() -> void:
	var mt := _tree()
	if is_instance_valid(mt) \
			and mt.peer_disconnected.is_connected(_on_peer_disconnected):
		mt.peer_disconnected.disconnect(_on_peer_disconnected)
	NetwServices.unregister(self, InterestService)


func _on_peer_disconnected(peer_id: int) -> void:
	_visibility_relay.erase(peer_id)
	_observer_relay.erase(peer_id)


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
	var layer_ids: Dictionary = _entity_layers.get(entity, {})
	for layer_id: StringName in layer_ids:
		var layer := get_layer(layer_id)
		if layer and layer.has_entity(entity) and layer.verdict_for(peer_id):
			return true
	return false


func _on_layer_policy_changed(layer: NetwInterestLayer) -> void:
	_mark_layer_dirty(layer)
	if _is_server():
		_drive_layer(layer)
	if _gates.has(layer.layer_id):
		_mark_gate_dirty(layer.layer_id)


func _on_layer_viewer_changed(
		layer: NetwInterestLayer, peer_id: int, added: bool) -> void:
	if _gates.has(layer.layer_id):
		_mark_gate_dirty(layer.layer_id)
	_mark_layer_dirty(layer)
	if _is_server():
		_drive_layer(layer)


func _on_layer_entity_changed(
		layer: NetwInterestLayer, entity: NetwEntity, added: bool) -> void:
	if added:
		_track_entity_layer(entity, layer.layer_id)
		_install_entity_filter(entity)
	else:
		_untrack_entity_layer(entity, layer.layer_id)
	_mark_entity_dirty(entity)
	if added and _is_server():
		_drive_layer(layer)


## Registers [param gate] as the network carrier for its layer.
func register_gate(gate: InterestGate) -> void:
	if not is_instance_valid(gate):
		return
	if gate.layer_id.is_empty():
		return
	var existing: InterestGate = _gates.get(gate.layer_id)
	if is_instance_valid(existing) and existing != gate:
		push_error(
				"InterestService: gate already registered for layer '%s'"
				% [String(gate.layer_id)])
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
	var layers: Dictionary = _entity_layers.get_or_add(entity, {})
	layers[layer_id] = true


func _untrack_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	var layers: Dictionary = _entity_layers.get(entity, {})
	layers.erase(layer_id)
	if layers.is_empty():
		_entity_layers.erase(entity)


func _install_entity_filter(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
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
	var layer_ids: Dictionary = _entity_layers.get(entity, {}).duplicate()
	for layer_id: StringName in layer_ids:
		var layer := get_layer(layer_id)
		if layer:
			layer.remove_entity(entity)
	_uninstall_entity_filter(entity)


func _mark_layer_dirty(layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in layer._entities:
		_mark_entity_dirty(entity)


func _mark_entity_dirty(entity: NetwEntity) -> void:
	if entity == null:
		return
	_dirty_entities[entity] = true
	_schedule_visibility_flush()


func _mark_gate_dirty(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
	_dirty_gate_layers[layer_id] = true
	_schedule_visibility_flush()


func _schedule_visibility_flush() -> void:
	if _refresh_scheduled:
		return
	_refresh_scheduled = true
	_flush_visibility.call_deferred()


## Flushes gate snapshots, entity visibility, and observer events.
func flush() -> void:
	_refresh_scheduled = false
	_flush_gate_snapshots()
	_flush_entity_visibility()
	_flush_visibility_relay()
	_flush_observer_relay()


## Flushes only bound gate snapshots.
##
## Use before spawning a subtree whose admission gate must be visible
## before child spawn packets are sent.
func flush_gates() -> void:
	_flush_gate_snapshots()


func _flush_visibility() -> void:
	flush()


func _flush_gate_snapshots() -> void:
	for layer_id: StringName in _dirty_gate_layers.keys():
		var gate: InterestGate = _gates.get(layer_id)
		var layer := get_layer(layer_id)
		if not is_instance_valid(gate) or layer == null:
			continue
		gate.apply_snapshot(layer.viewers_packed(), layer.policy)
	_dirty_gate_layers.clear()


func _flush_entity_visibility() -> void:
	for entity: NetwEntity in _dirty_entities.keys():
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		for sync in entity.synchronizers():
			if is_instance_valid(sync) and sync.is_inside_tree():
				sync.update_visibility()
	_dirty_entities.clear()


func _drive_layer(layer: NetwInterestLayer) -> void:
	if layer == null:
		return
	layer.drive_now(_live_peers(layer))


func _live_peers(layer: NetwInterestLayer) -> Array[int]:
	var seen: Dictionary[int, bool] = {}
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
		entity: NetwEntity, peer_id: int,
		layer: NetwInterestLayer) -> void:
	_queue_visibility_event(layer, entity, peer_id, ObserverEvent.ENTER)
	_queue_observer_event(layer, entity, peer_id, ObserverEvent.ENTER)


func _on_layer_interest_exit(
		entity: NetwEntity, peer_id: int,
		layer: NetwInterestLayer) -> void:
	_queue_visibility_event(layer, entity, peer_id, ObserverEvent.EXIT)
	_queue_observer_event(layer, entity, peer_id, ObserverEvent.EXIT)


func _queue_visibility_event(
		layer: NetwInterestLayer, entity: NetwEntity,
		observer_peer: int, kind: int) -> void:
	if not _is_server():
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
	if observer_peer == 0 or observer_peer == MultiplayerPeer.TARGET_PEER_SERVER:
		return
	if not entity.owner.is_inside_tree():
		return
	var mt := _tree()
	if not _can_send_rpc_to_peer(mt, observer_peer):
		return
	var bucket: Array = _visibility_relay.get_or_add(observer_peer, [])
	bucket.append([
		mt.get_path_to(entity.owner),
		layer.layer_id,
		kind,
	])
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
		_rpc_visibility_events.rpc_id(observer_peer, events)
	_visibility_relay.clear()


func _queue_observer_event(
		layer: NetwInterestLayer, entity: NetwEntity,
		observer_peer: int, kind: int) -> void:
	if not _is_server():
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
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
	bucket.append([
		mt.get_path_to(entity.owner),
		layer.layer_id,
		observer_peer,
		kind,
	])
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
		_rpc_observer_events.rpc_id(owner_peer, events)
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
		if typeof(raw) != TYPE_ARRAY or (raw as Array).size() != 4:
			continue
		var path: NodePath = raw[0]
		var layer_id: StringName = raw[1]
		var observer_peer: int = raw[2]
		var kind: int = raw[3]
		var node := mt.get_node_or_null(path)
		if not is_instance_valid(node):
			continue
		var entity := NetwEntity.of(node)
		if not entity:
			continue
		if kind == ObserverEvent.ENTER:
			entity.observer_entered.emit(layer_id, observer_peer)
		else:
			entity.observer_left.emit(layer_id, observer_peer)


@rpc("authority", "call_remote", "reliable")
func _rpc_visibility_events(events: Array) -> void:
	for raw in events:
		if not _apply_visibility_event(raw):
			_pending_visibility_events.append([raw, 0])
	if not _pending_visibility_events.is_empty():
		_flush_pending_visibility_events.call_deferred()


func _apply_visibility_event(raw: Variant) -> bool:
	if typeof(raw) != TYPE_ARRAY or (raw as Array).size() != 3:
		return true
	var path: NodePath = raw[0]
	var layer_id: StringName = raw[1]
	var kind: int = raw[2]
	var mt := _tree()
	if not is_instance_valid(mt):
		return false
	var node := mt.get_node_or_null(path)
	if not is_instance_valid(node):
		return kind != ObserverEvent.ENTER
	var entity := NetwEntity.of(node)
	if not entity:
		return true
	var layer := layer_for(layer_id)
	if not layer:
		return true
	if kind == ObserverEvent.ENTER:
		layer.entity_visible.emit(entity)
	else:
		layer.entity_hidden.emit(entity)
	return true


func _flush_pending_visibility_events() -> void:
	var pending := _pending_visibility_events
	_pending_visibility_events = []
	for item in pending:
		if typeof(item) != TYPE_ARRAY or (item as Array).size() != 2:
			continue
		var raw: Variant = item[0]
		var attempts: int = item[1]
		if not _apply_visibility_event(raw) and attempts < 30:
			_pending_visibility_events.append([raw, attempts + 1])
	if not _pending_visibility_events.is_empty():
		_flush_pending_visibility_events.call_deferred()
