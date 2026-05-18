## Facade for the tree-scoped [InterestService].
##
## One instance lives on [member MultiplayerTree.interest]. It keeps
## component code on the [code]Netw*[/code] facade API while the node
## service owns layer state, RPCs, and engine visibility filters.
class_name NetwInterest
extends RefCounted


var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns [code]true[/code] while the underlying tree is alive.
func is_valid() -> bool:
	return is_instance_valid(_tree())


## Returns the backing [InterestService], or [code]null[/code].
func get_service() -> InterestService:
	var mt := _tree()
	if not mt:
		return null
	var service := mt.get_service(InterestService) as InterestService
	if service:
		return service
	return mt.find_service_node(InterestService) as InterestService


## Returns the layer for [param layer_id], creating it when missing.
func layer_for(layer_id: StringName) -> NetwInterestLayer:
	var service := get_service()
	return service.layer_for(layer_id) if service else null


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	var service := get_service()
	return service.get_layer(layer_id) if service else null


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var service := get_service()
	if service:
		return service.all_layers()
	var out: Array[NetwInterestLayer] = []
	return out


## Registers [param anchor] as a compatibility adapter.
func register_anchor(anchor: InterestSynchronizer) -> void:
	var service := get_service()
	if service:
		service.register_anchor(anchor)


## Removes [param anchor] from compatibility lookup.
func unregister_anchor(anchor: InterestSynchronizer) -> void:
	var service := get_service()
	if service:
		service.unregister_anchor(anchor)


## Deprecated compatibility lookup for adapter nodes.
func anchor_for(layer_id: StringName) -> InterestSynchronizer:
	var service := get_service()
	return service.anchor_for(layer_id) if service else null


## Returns registered compatibility anchors.
func all_anchors() -> Array[InterestSynchronizer]:
	var service := get_service()
	if service:
		return service.all_anchors()
	var out: Array[InterestSynchronizer] = []
	return out


## Sets [param layer_id]'s policy and mirrors the change from servers.
func set_policy(layer_id: StringName, policy: int) -> void:
	var service := get_service()
	if service:
		service.set_policy(layer_id, policy)


## Adds [param peer_id] as a viewer of [param layer_id].
func add_viewer(layer_id: StringName, peer_id: int) -> void:
	var service := get_service()
	if service:
		service.add_viewer(layer_id, peer_id)


## Removes [param peer_id] as a viewer of [param layer_id].
func remove_viewer(layer_id: StringName, peer_id: int) -> void:
	var service := get_service()
	if service:
		service.remove_viewer(layer_id, peer_id)


## Registers [param entity] as a member of [param layer_id].
func register_entity_for_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	var service := get_service()
	if service:
		service.register_entity_for_layer(layer_id, entity)


## Reverses [method register_entity_for_layer].
func unregister_entity_from_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	var service := get_service()
	if service:
		service.unregister_entity_from_layer(layer_id, entity)


## Returns [code]true[/code] if any entity layer admits [param peer_id].
func can_peer_see_entity(peer_id: int, entity: NetwEntity) -> bool:
	var service := get_service()
	return service.can_peer_see_entity(peer_id, entity) if service else false


## Applies a server-origin layer config mirror.
func receive_layer_config(layer_id: StringName, policy: int) -> void:
	var service := get_service()
	if service:
		service.receive_layer_config(layer_id, policy)


## Applies a server-origin viewer mirror.
func receive_viewer_delta(
		layer_id: StringName, peer_id: int, added: bool) -> void:
	var service := get_service()
	if service:
		service.receive_viewer_delta(layer_id, peer_id, added)


## Applies a server-origin entity mirror.
func receive_entity_delta(
		layer_id: StringName,
		entity_path: String,
		added: bool) -> void:
	var service := get_service()
	if service:
		service.receive_entity_delta(layer_id, entity_path, added)


## Flushes pending engine visibility refreshes.
func flush_visibility() -> void:
	var service := get_service()
	if service:
		service.flush_visibility()


func _tree() -> MultiplayerTree:
	return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null
