## Session service that owns interest layers, mirrors, and filters.
##
## [MultiplayerTree] creates this service before other session services
## enter the tree. Use [member MultiplayerTree.interest] for the facade
## API unless node-service access is explicitly needed.
class_name InterestService
extends Node


var _layers: Dictionary[StringName, NetwInterestLayer] = {}
var _anchors: Dictionary[StringName, InterestSynchronizer] = {}
var _entity_layers: Dictionary[NetwEntity, Dictionary] = {}
var _entity_filters: Dictionary[NetwEntity, Callable] = {}
var _entity_exit_handlers: Dictionary[NetwEntity, Callable] = {}
var _dirty_entities: Dictionary[NetwEntity, bool] = {}
var _refresh_scheduled: bool = false


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
	layer = NetwInterestLayer.new(layer_id)
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


## Registers [param anchor] as a compatibility adapter.
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


## Removes [param anchor] from compatibility lookup.
func unregister_anchor(anchor: InterestSynchronizer) -> void:
	if not is_instance_valid(anchor):
		return
	if _anchors.get(anchor.layer_id) == anchor:
		_anchors.erase(anchor.layer_id)


## Deprecated compatibility lookup for adapter nodes.
func anchor_for(layer_id: StringName) -> InterestSynchronizer:
	return _anchors.get(layer_id)


## Returns registered compatibility anchors.
func all_anchors() -> Array[InterestSynchronizer]:
	var out: Array[InterestSynchronizer] = []
	out.assign(_anchors.values())
	return out


## Sets [param layer_id]'s policy and mirrors the change from servers.
func set_policy(layer_id: StringName, policy: int) -> void:
	var layer := layer_for(layer_id)
	if not layer or not layer.set_policy(policy):
		return
	_mark_layer_dirty(layer)
	_drive_layer_if_unanchored(layer)
	_broadcast_layer_config(layer)


## Adds [param peer_id] as a viewer of [param layer_id].
func add_viewer(layer_id: StringName, peer_id: int) -> void:
	var layer := layer_for(layer_id)
	if not layer or not layer.add_viewer(peer_id):
		return
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if is_instance_valid(anchor):
		anchor._mirror_viewer_from_interest(peer_id, true)
	_mark_layer_dirty(layer)
	_drive_layer_if_unanchored(layer)
	_broadcast_viewer_delta(layer_id, peer_id, true)


## Removes [param peer_id] as a viewer of [param layer_id].
func remove_viewer(layer_id: StringName, peer_id: int) -> void:
	var layer := get_layer(layer_id)
	if not layer or not layer.remove_viewer(peer_id):
		return
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if is_instance_valid(anchor):
		anchor._mirror_viewer_from_interest(peer_id, false)
	_mark_layer_dirty(layer)
	_drive_layer_if_unanchored(layer)
	_broadcast_viewer_delta(layer_id, peer_id, false)


## Registers [param entity] as a member of [param layer_id].
func register_entity_for_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	var layer := layer_for(layer_id)
	if not layer or not layer.add_entity(entity):
		return
	_track_entity_layer(entity, layer_id)
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if is_instance_valid(anchor):
		anchor._mirror_entity_from_interest(entity, true)
	_install_entity_filter(entity)
	_mark_entity_dirty(entity)
	_drive_layer_if_unanchored(layer)
	_broadcast_entity_delta(layer_id, entity, true)


## Reverses [method register_entity_for_layer].
func unregister_entity_from_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	var layer := get_layer(layer_id)
	if not layer or not layer.remove_entity(entity):
		return
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if is_instance_valid(anchor):
		anchor._mirror_entity_from_interest(entity, false)
	_untrack_entity_layer(entity, layer_id)
	_mark_entity_dirty(entity)
	_broadcast_entity_delta(layer_id, entity, false)


## Returns [code]true[/code] if any entity layer admits [param peer_id].
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


## Applies a server-origin layer config mirror.
func receive_layer_config(layer_id: StringName, policy: int) -> void:
	var layer := layer_for(layer_id)
	if not layer:
		return
	if layer.set_policy(policy):
		_mark_layer_dirty(layer)
		_drive_layer_if_unanchored(layer)


## Applies a server-origin viewer mirror.
func receive_viewer_delta(
		layer_id: StringName, peer_id: int, added: bool) -> void:
	if added:
		add_viewer(layer_id, peer_id)
	else:
		remove_viewer(layer_id, peer_id)


## Applies a server-origin entity mirror.
func receive_entity_delta(
		layer_id: StringName,
		entity_path: String,
		added: bool) -> void:
	var entity := _entity_from_path(entity_path)
	if not entity:
		return
	if added:
		register_entity_for_layer(layer_id, entity)
	else:
		unregister_entity_from_layer(layer_id, entity)


## Flushes pending engine visibility refreshes.
func flush_visibility() -> void:
	_refresh_scheduled = false
	for entity: NetwEntity in _dirty_entities.keys():
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		for sync in entity.synchronizers():
			if is_instance_valid(sync) and sync.is_inside_tree():
				sync.update_visibility()
	_dirty_entities.clear()


@rpc("authority", "call_remote", "reliable")
func _rpc_interest_layer_config(layer_id: String, policy: int) -> void:
	if not _is_rpc_from_server("_rpc_interest_layer_config"):
		return
	receive_layer_config(StringName(layer_id), policy)


@rpc("authority", "call_remote", "reliable")
func _rpc_interest_viewer_delta(
		layer_id: String,
		peer_id: int,
		added: bool) -> void:
	if not _is_rpc_from_server("_rpc_interest_viewer_delta"):
		return
	receive_viewer_delta(StringName(layer_id), peer_id, added)


@rpc("authority", "call_remote", "reliable")
func _rpc_interest_entity_delta(
		layer_id: String,
		entity_path: String,
		added: bool) -> void:
	if not _is_rpc_from_server("_rpc_interest_entity_delta"):
		return
	receive_entity_delta(StringName(layer_id), entity_path, added)


func _flush_interest_visibility() -> void:
	flush_visibility()


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
		unregister_entity_from_layer(layer_id, entity)
	_uninstall_entity_filter(entity)


func _mark_layer_dirty(layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in layer.entities:
		_mark_entity_dirty(entity)


func _mark_entity_dirty(entity: NetwEntity) -> void:
	if entity == null:
		return
	_dirty_entities[entity] = true
	_schedule_visibility_flush()


func _schedule_visibility_flush() -> void:
	if _refresh_scheduled:
		return
	_refresh_scheduled = true
	_flush_interest_visibility.call_deferred()


func _drive_layer_if_unanchored(layer: NetwInterestLayer) -> void:
	if layer == null or _anchors.has(layer.layer_id):
		return
	_drive_layer(layer)


func _drive_layer(layer: NetwInterestLayer) -> void:
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
