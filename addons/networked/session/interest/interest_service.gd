## Owns layer storage, RPC mirroring, and engine-side visibility
## flushes for a [MultiplayerTree].
##
## External callers do not use this service directly: they go through
## [NetwInterest] to obtain a [NetwInterestLayer] and mutate the
## layer, which calls back into the [code]_on_layer_*[/code] hooks
## here.
class_name InterestService
extends Node


var _layers: Dictionary[StringName, NetwInterestLayer] = {}
var _anchors: Dictionary[StringName, InterestSynchronizer] = {}
var _gates: Dictionary[StringName, InterestGate] = {}
var _entity_layers: Dictionary[NetwEntity, Dictionary] = {}
var _entity_filters: Dictionary[NetwEntity, Callable] = {}
var _entity_exit_handlers: Dictionary[NetwEntity, Callable] = {}
var _dirty_entities: Dictionary[NetwEntity, bool] = {}
var _dirty_gate_layers: Dictionary[StringName, bool] = {}
var _refresh_scheduled: bool = false
var _suppress_broadcast: bool = false


func _enter_tree() -> void:
	NetwServices.register(self, InterestService)


func _exit_tree() -> void:
	NetwServices.unregister(self, InterestService)


## Returns the layer for [param layer_id], creating it when missing.
func layer_for(layer_id: StringName) -> NetwInterestLayer:
	if layer_id.is_empty():
		return null
	var layer: NetwInterestLayer = _layers.get(layer_id)
	if layer:
		return layer
	layer = NetwInterestLayer.new(layer_id, self)
	_layers[layer_id] = layer
	return layer


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	return _layers.get(layer_id)


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var out: Array[NetwInterestLayer] = []
	out.assign(_layers.values())
	return out


## Returns [code]true[/code] if any entity layer admits [param peer_id].
## Used by the spawn-visibility integration to gate spawn packets.
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


# ---------------------------------------------------------------------------
# Layer mutation hooks. Called by [NetwInterestLayer] mutators; do not
# call directly.
# ---------------------------------------------------------------------------

func _on_layer_policy_changed(layer: NetwInterestLayer) -> void:
	_mark_layer_dirty(layer)
	_drive_layer_if_unanchored(layer)
	if _gates.has(layer.layer_id) and not _suppress_broadcast:
		_mark_gate_dirty(layer.layer_id)
	if not _suppress_broadcast:
		_broadcast_layer_config(layer)


func _on_layer_viewer_changed(
		layer: NetwInterestLayer, peer_id: int, added: bool) -> void:
	var anchor: InterestSynchronizer = _anchors.get(layer.layer_id)
	if is_instance_valid(anchor):
		anchor._mirror_viewer_from_interest(peer_id, added)
	if _gates.has(layer.layer_id) and not _suppress_broadcast:
		_mark_gate_dirty(layer.layer_id)
	_mark_layer_dirty(layer)
	_drive_layer_if_unanchored(layer)
	if not _suppress_broadcast:
		_broadcast_viewer_delta(layer.layer_id, peer_id, added)


func _on_layer_entity_changed(
		layer: NetwInterestLayer, entity: NetwEntity, added: bool) -> void:
	var anchor: InterestSynchronizer = _anchors.get(layer.layer_id)
	if added:
		_track_entity_layer(entity, layer.layer_id)
		if is_instance_valid(anchor):
			anchor._mirror_entity_from_interest(entity, true)
		_install_entity_filter(entity)
	else:
		if is_instance_valid(anchor):
			anchor._mirror_entity_from_interest(entity, false)
		_untrack_entity_layer(entity, layer.layer_id)
	_mark_entity_dirty(entity)
	if added:
		_drive_layer_if_unanchored(layer)
	if not _suppress_broadcast:
		_broadcast_entity_delta(layer.layer_id, entity, added)


# ---------------------------------------------------------------------------
# Anchor compat. Deleted in Phase 2 when [InterestSynchronizer] goes
# away.
# ---------------------------------------------------------------------------

func register_anchor(anchor: InterestSynchronizer) -> void:
	if not is_instance_valid(anchor):
		return
	if anchor.layer_id.is_empty():
		return
	_anchors[anchor.layer_id] = anchor
	var layer := layer_for(anchor.layer_id)
	for entity: NetwEntity in layer.entities:
		anchor._mirror_entity_from_interest(entity, true)
	for peer_id: int in layer.viewers:
		anchor._mirror_viewer_from_interest(peer_id, true)


func unregister_anchor(anchor: InterestSynchronizer) -> void:
	if not is_instance_valid(anchor):
		return
	if _anchors.get(anchor.layer_id) == anchor:
		_anchors.erase(anchor.layer_id)


func anchor_for(layer_id: StringName) -> InterestSynchronizer:
	return _anchors.get(layer_id)


func all_anchors() -> Array[InterestSynchronizer]:
	var out: Array[InterestSynchronizer] = []
	out.assign(_anchors.values())
	return out


# ---------------------------------------------------------------------------
# Gate registry. New in Phase 2; replaces anchor registry in Phase 3.
# ---------------------------------------------------------------------------

## Registers [param gate] as the network carrier for its
## [member InterestGate.layer_id]. Errors if another gate is already
## registered for the same id. Called by [InterestGate._enter_tree].
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
	if layer:
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


# ---------------------------------------------------------------------------
# RPC receive paths. Suppress re-broadcast while applying remote
# mirrors.
# ---------------------------------------------------------------------------

@rpc("authority", "call_remote", "reliable")
func _rpc_interest_layer_config(layer_id: String, policy: int) -> void:
	if not _is_rpc_from_server("_rpc_interest_layer_config"):
		return
	_apply_remote_policy(StringName(layer_id), policy)


@rpc("authority", "call_remote", "reliable")
func _rpc_interest_viewer_delta(
		layer_id: String,
		peer_id: int,
		added: bool) -> void:
	if not _is_rpc_from_server("_rpc_interest_viewer_delta"):
		return
	_apply_remote_viewer(StringName(layer_id), peer_id, added)


@rpc("authority", "call_remote", "reliable")
func _rpc_interest_entity_delta(
		layer_id: String,
		entity_path: String,
		added: bool) -> void:
	if not _is_rpc_from_server("_rpc_interest_entity_delta"):
		return
	var entity := _entity_from_path(entity_path)
	if not entity:
		return
	_apply_remote_entity(StringName(layer_id), entity, added)


func _apply_remote_policy(layer_id: StringName, policy: int) -> void:
	var layer := layer_for(layer_id)
	if not layer:
		return
	_suppress_broadcast = true
	layer.set_policy(policy)
	_suppress_broadcast = false


func _apply_remote_viewer(
		layer_id: StringName, peer_id: int, added: bool) -> void:
	var layer := layer_for(layer_id)
	if not layer:
		return
	_suppress_broadcast = true
	if added:
		layer.add_viewer(peer_id)
	else:
		layer.remove_viewer(peer_id)
	_suppress_broadcast = false


func _apply_remote_entity(
		layer_id: StringName, entity: NetwEntity, added: bool) -> void:
	var layer := layer_for(layer_id)
	if not layer:
		return
	_suppress_broadcast = true
	if added:
		layer.add_entity(entity)
	else:
		layer.remove_entity(entity)
	_suppress_broadcast = false


# ---------------------------------------------------------------------------
# Engine-effect: per-entity visibility filter management.
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Visibility flush scheduling.
# ---------------------------------------------------------------------------

func _mark_layer_dirty(layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in layer.entities:
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


## Synchronously flushes pending gate snapshots and entity visibility
## updates. Normally invoked via [code]call_deferred[/code] at end of
## frame; tests call this directly to observe effects.
func flush() -> void:
	_refresh_scheduled = false
	_flush_gate_snapshots()
	_flush_entity_visibility()


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


func _drive_layer_if_unanchored(layer: NetwInterestLayer) -> void:
	if layer == null or _anchors.has(layer.layer_id):
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


# ---------------------------------------------------------------------------
# RPC broadcast helpers.
# ---------------------------------------------------------------------------

func _broadcast_layer_config(layer: NetwInterestLayer) -> void:
	var mt := _tree()
	if not _can_broadcast(mt):
		return
	for peer_id: int in mt.multiplayer_api.get_peers():
		_rpc_interest_layer_config.rpc_id(
				peer_id, String(layer.layer_id), layer.policy)


func _broadcast_viewer_delta(
		layer_id: StringName, peer_id: int, added: bool) -> void:
	var mt := _tree()
	if not _can_broadcast(mt):
		return
	for target_id: int in mt.multiplayer_api.get_peers():
		_rpc_interest_viewer_delta.rpc_id(
				target_id, String(layer_id), peer_id, added)


func _broadcast_entity_delta(
		layer_id: StringName,
		entity: NetwEntity,
		added: bool) -> void:
	var mt := _tree()
	if not _can_broadcast(mt):
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
	var entity_path := String(mt.get_path_to(entity.owner))
	for peer_id: int in mt.multiplayer_api.get_peers():
		_rpc_interest_entity_delta.rpc_id(
				peer_id, String(layer_id), entity_path, added)


func _entity_from_path(entity_path: String) -> NetwEntity:
	var mt := _tree()
	if not is_instance_valid(mt):
		return null
	var node := mt.get_node_or_null(NodePath(entity_path))
	return NetwEntity.of(node) if is_instance_valid(node) else null


func _is_rpc_from_server(rpc_name: String) -> bool:
	var sender := multiplayer.get_remote_sender_id()
	if sender == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	Netw.dbg.warn("%s received from non-server peer %d", [rpc_name, sender])
	return false


func _can_broadcast(mt: MultiplayerTree) -> bool:
	return is_instance_valid(mt) and mt.is_server \
			and mt.multiplayer_api != null \
			and mt.multiplayer_peer != null


func _tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)
