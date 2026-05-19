## Per-tree scheduler that batches [NetwInterestLayer] mutations and
## applies their engine-side effects.
##
## One [InterestService] lives under each [MultiplayerTree], registered
## via [NetwServices]. External code never calls it directly; it
## reacts to layer mutators through hooks and exposes
## [method gate_for] / [method layer_for] for [InterestGate] binding.
##
## [br][br]
## Per frame: viewer / policy / entity changes mark the layer dirty;
## a deferred [method flush] computes per-(entity, peer) transitions,
## emits [signal NetwInterestLayer.interest_enter] /
## [signal NetwInterestLayer.interest_exit], calls
## [method MultiplayerSynchronizer.update_visibility] on each
## entity synchronizer, and applies the bound gate's snapshot. The
## service is not the network transport — bound-layer admission
## crosses the wire via the gate's own
## [MultiplayerSynchronizer], not service RPCs.
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


# ---------------------------------------------------------------------------
# Gate registry.
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


## Synchronously flushes only gate snapshots/admission. Use before
## spawning under a gate so the gated subtree is admitted before the
## child spawn packet is emitted, while entity visibility remains
## dirty until the child is in-tree.
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
