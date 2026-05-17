## Declares which interest layers an entity participates in.
##
## Sibling component placed under a [NetwEntity] root. Mirrors the
## [SaveComponent] / [SpawnerComponent] pattern: contributes its
## [member layer_ids] property to the spawner's spawn packet so the
## value lands on the client before the entity enters its tree, then
## registers the entity with the matching [InterestSynchronizer]
## anchors through [NetwInterest].
##
## [codeblock]
##     # In the entity scene:
##     %InterestComponent.layer_ids = [&"arena:1", &"team:blue"]
## [/codeblock]
##
## Membership transports as part of the entity's own spawn-sync; no
## NodePaths or RPC mirroring on the wire.
class_name InterestComponent
extends Node


## Stable layer ids this entity belongs to. Resolved against
## [member NetwInterest._anchors] at tree-enter. Mutating after
## tree-enter is supported on the server; the new set replaces the
## previous registration on the next driver pass.
@export var layer_ids: Array[StringName] = []:
	set(value):
		var prev := layer_ids.duplicate()
		layer_ids = value
		if is_inside_tree():
			_apply_layer_diff(prev, layer_ids)


func _init() -> void:
	name = "InterestComponent"
	unique_name_in_owner = true


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
	for id in layer_ids:
		_register_for(id)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	for id in layer_ids:
		_unregister_for(id)


func _register_for(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
	var entity := _resolve_entity()
	if not entity:
		return
	var interest := _resolve_interest()
	if not interest:
		return
	interest.register_entity_for_layer(layer_id, entity)


func _unregister_for(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
	var entity := _resolve_entity()
	if not entity:
		return
	var interest := _resolve_interest()
	if not interest:
		return
	interest.unregister_entity_from_layer(layer_id, entity)


func _apply_layer_diff(
		prev: Array[StringName], next: Array[StringName]) -> void:
	for id in prev:
		if id not in next:
			_unregister_for(id)
	for id in next:
		if id not in prev:
			_register_for(id)


func _resolve_entity() -> NetwEntity:
	if not is_instance_valid(owner):
		return null
	return NetwEntity.of(owner)


func _resolve_interest() -> NetwInterest:
	var mt := MultiplayerTree.resolve(self)
	return mt.interest if mt else null
