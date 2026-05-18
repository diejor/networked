## Tree-scoped registry that connects [InterestSynchronizer] anchors
## to [NetwEntity] members by [code]layer_id[/code].
##
## One instance lives on [member MultiplayerTree.interest] and is
## available from any node via [code]Netw.ctx(self).interest[/code].
## Anchors register themselves on tree entry; entities register through
## the [code]layer_ids[/code] property contributed by
## [InterestComponent]. The pending queue absorbs either registration
## order so a late-arriving anchor still picks up earlier entities and
## vice versa.
##
## [codeblock]
##     var arena := Netw.ctx(self).interest.anchor_for(&"arena:1")
##     if arena:
##         arena.add_viewer(player.peer_id)
## [/codeblock]
##
## This registry holds no replication state itself. Visibility filters
## live on the [InterestSynchronizer] anchors; this class only
## indexes them.
class_name NetwInterest
extends RefCounted


var _tree_ref: WeakRef
var _anchors: Dictionary[StringName, InterestSynchronizer] = {}
var _pending_entities: Dictionary[StringName, Array] = {}


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Registers [param anchor] under its [member
## InterestSynchronizer.layer_id]. Drains any entities that registered
## for the same id while no anchor was present. Idempotent.
func register_anchor(anchor: InterestSynchronizer) -> void:
	if not is_instance_valid(anchor):
		return
	var id := anchor.layer_id
	if id.is_empty():
		return
	if _anchors.get(id) == anchor:
		return
	_anchors[id] = anchor
	var pending: Array = _pending_entities.get(id, [])
	for entity: NetwEntity in pending:
		if is_instance_valid(entity) and is_instance_valid(entity.owner):
			anchor.add_entity(entity)
	_pending_entities.erase(id)


## Removes [param anchor] if it is the currently registered entry
## under its [code]layer_id[/code]. Idempotent.
func unregister_anchor(anchor: InterestSynchronizer) -> void:
	if not is_instance_valid(anchor):
		return
	var id := anchor.layer_id
	if id.is_empty():
		return
	if _anchors.get(id) == anchor:
		_anchors.erase(id)


## Returns the [InterestSynchronizer] registered under [param layer_id],
## or [code]null[/code] when no anchor has registered with that id.
func anchor_for(layer_id: StringName) -> InterestSynchronizer:
	return _anchors.get(layer_id)


## Returns every registered anchor as an array.
func all_anchors() -> Array[InterestSynchronizer]:
	var out: Array[InterestSynchronizer] = []
	out.assign(_anchors.values())
	return out


## Registers [param entity] as a member of [param layer_id]. Enrolls
## immediately when the anchor is already known; queues otherwise.
func register_entity_for_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	if layer_id.is_empty() or entity == null:
		return
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if anchor:
		anchor.add_entity(entity)
		return
	var queue: Array = _pending_entities.get_or_add(layer_id, [])
	if entity not in queue:
		queue.append(entity)


## Reverses [method register_entity_for_layer]. Idempotent.
func unregister_entity_from_layer(
		layer_id: StringName, entity: NetwEntity) -> void:
	if layer_id.is_empty() or entity == null:
		return
	var anchor: InterestSynchronizer = _anchors.get(layer_id)
	if anchor:
		anchor.remove_entity(entity)
	var queue: Array = _pending_entities.get(layer_id, [])
	queue.erase(entity)
	if queue.is_empty():
		_pending_entities.erase(layer_id)
