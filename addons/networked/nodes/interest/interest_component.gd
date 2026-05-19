## Declares which [NetwInterestLayer]s an entity participates in.
##
## Sibling under a [NetwEntity] root. [member layer_ids] is
## contributed to the spawn packet via
## [method NetwEntity.contribute_spawn_property], so the value lands
## on the client before the entity enters its tree. On the server,
## tree-enter registers the entity with each named layer through
## [NetwInterest]; tree-exit unregisters. Mutating [member layer_ids]
## at runtime applies the diff incrementally.
##
## [br][br]
## Entity membership is server-only state — registration is gated on
## [method MultiplayerAPI.is_server], and clients carry no entity set.
## For scene-level enrollment use [method MultiplayerScene.register_player]
## instead; this component is for [b]additional[/b] layers (AoI rings,
## team buckets, stealth bubbles) the entity opts into.
## [codeblock]
## # In the entity scene:
## %InterestComponent.layer_ids = [&"arena:1", &"team:blue"]
## [/codeblock]
class_name InterestComponent
extends Node


## Stable layer ids this entity belongs to. Resolved against
## [NetwInterestLayer] entries at tree-enter. Mutating after
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
	if not _is_server():
		return
	for id in layer_ids:
		_register_for(id)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if not _is_server():
		return
	for id in layer_ids:
		_unregister_for(id)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()


func _register_for(layer_id: StringName) -> void:
	if layer_id.is_empty():
		return
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
	# Walks from self via [method NetwEntity.of] so the resolution
	# works whether or not [member Node.owner] has been assigned
	# (packed-scene placement sets it; programmatic add_child does not).
	return NetwEntity.of(self)


func _resolve_interest() -> NetwInterest:
	var mt := MultiplayerTree.resolve(self)
	return mt.interest if mt else null
