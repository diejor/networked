## Declares extra [NetwInterestLayer] memberships for an entity.
##
## Scene membership is owned by [MultiplayerScene]; this component adds
## optional layers such as teams, sight cones, proximity buckets, or
## stealth zones. [member layer_ids] is spawn-synced so client tools can
## read the labels, but entity membership and visibility decisions stay
## server-owned.
##
## [br][br]
## Add this as a sibling under the entity root. Mutating
## [member layer_ids] while the node is in the tree updates server-side
## layer membership immediately. Set [member report_observers] only when
## the owner client needs to know which other peers can see this entity.
## [codeblock]
## # Server: this entity participates in two extra layers.
## %InterestComponent.layer_ids = [&"arena:1", &"team:blue"]
##
## # Owner client: react when other peers observe this entity.
## %InterestComponent.report_observers = true
## %InterestComponent.observer_entered.connect(func(layer_id, peer_id):
##     show_seen_by(peer_id)
## )
## [/codeblock]
class_name InterestComponent
extends Node


## Emitted on the owner client when [param peer_id] starts observing
## this entity through [param layer_id].
signal observer_entered(layer_id: StringName, peer_id: int)

## Emitted on the owner client when [param peer_id] stops observing
## this entity through [param layer_id].
signal observer_left(layer_id: StringName, peer_id: int)


## Stable extra layer ids this entity belongs to.
##
## The server registers the entity with these layers. Clients receive
## the value for local UI/debugging only; client-side layer entity sets
## remain empty.
@export var layer_ids: Array[StringName] = []:
	set(value):
		var prev := layer_ids.duplicate()
		layer_ids = value
		if is_inside_tree():
			_apply_layer_diff(prev, layer_ids)

## Enables owner-client observer transitions for this entity.
##
## This answers "who can see me?" Client code asking "what can I see?"
## should connect to [signal NetwInterestLayer.entity_visible] instead.
@export var report_observers: bool = false


func _init() -> void:
	name = "InterestComponent"
	unique_name_in_owner = true


## Returns the [InterestComponent] sibling under [param entity]'s root.
static func of(entity: NetwEntity) -> InterestComponent:
	if entity == null or not is_instance_valid(entity.owner):
		return null
	return entity.owner.get_node_or_null(^"%InterestComponent") \
			as InterestComponent


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED:
		return
	if Engine.is_editor_hint():
		return
	var entity := Netw.ctx(self).entity
	if not entity:
		return
	entity.contribute_spawn_property(NodePath("InterestComponent:layer_ids"))


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	_bind_observer_signals()
	if not _is_server():
		return
	for id in layer_ids:
		_register_for(id)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	_unbind_observer_signals()
	if not _is_server():
		return
	for id in layer_ids:
		_unregister_for(id)


func _bind_observer_signals() -> void:
	var entity := _resolve_entity()
	if not entity:
		return
	if not entity.observer_entered.is_connected(_on_entity_observer_entered):
		entity.observer_entered.connect(_on_entity_observer_entered)
	if not entity.observer_left.is_connected(_on_entity_observer_left):
		entity.observer_left.connect(_on_entity_observer_left)


func _unbind_observer_signals() -> void:
	var entity := _resolve_entity()
	if not entity:
		return
	if entity.observer_entered.is_connected(_on_entity_observer_entered):
		entity.observer_entered.disconnect(_on_entity_observer_entered)
	if entity.observer_left.is_connected(_on_entity_observer_left):
		entity.observer_left.disconnect(_on_entity_observer_left)


func _on_entity_observer_entered(layer_id: StringName, peer_id: int) -> void:
	observer_entered.emit(layer_id, peer_id)


func _on_entity_observer_left(layer_id: StringName, peer_id: int) -> void:
	observer_left.emit(layer_id, peer_id)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()


func _register_for(layer_id: StringName) -> void:
	assert(not layer_id.is_empty(),
			"InterestComponent: empty layer_id in layer_ids")
	var entity := _resolve_entity()
	if not entity:
		return
	var interest := _resolve_interest()
	if not interest:
		return
	interest.layer(layer_id).add_entity(entity)


func _unregister_for(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
	var entity := _resolve_entity()
	if not entity:
		return
	var interest := _resolve_interest()
	if not interest:
		return
	var layer := interest.get_layer(layer_id)
	if layer:
		layer.remove_entity(entity)


func _apply_layer_diff(
		prev: Array[StringName], next: Array[StringName]) -> void:
	for id in prev:
		if id not in next:
			_unregister_for(id)
	for id in next:
		if id not in prev:
			_register_for(id)


func _resolve_entity() -> NetwEntity:
	# Works for packed-scene placement and programmatic add_child.
	return NetwEntity.of(self)


func _resolve_interest() -> NetwInterest:
	var mt := MultiplayerTree.resolve(self)
	return mt.interest if mt else null
