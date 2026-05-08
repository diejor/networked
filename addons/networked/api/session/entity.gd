## Per-owner orchestration hub for networked entities.
##
## A [NetwEntity] is attached as metadata on the entity root (the
## [code]Node[/code] that hosts [SpawnerComponent], [SaveComponent], and
## sibling networked components). It exposes the parented-time signals
## those components and their siblings use to coordinate spawn-time
## property collection and lifecycle handoff.
##
## Access via [code]Netw.ctx(self).entity[/code] from any node inside the
## entity. The lookup walks to the entity root using [member Node.owner]
## when in-tree, or the parent chain when orphan (e.g. reacting to
## [constant Node.NOTIFICATION_PARENTED] before tree entry).
##
## [b]Signal order during the spawn phase[/b] (driven by
## [SpawnerComponent] when one is registered):
## [br]1. [signal collecting_spawn_properties] - siblings call
##     [method SpawnerComponent.add_spawn_property] to contribute paths.
## [br]2. [signal spawning] - siblings react with side effects
##     (e.g. [method SaveComponent.hydrate_from_db]).
## [br]3. [signal spawned] - emitted after scene registration.
##
## [signal collecting_save_properties] is emitted by [SaveComponent] in
## its own [code]_enter_tree[/code], independent of the spawn phase.
class_name NetwEntity
extends RefCounted

const META_KEY := &"netw_entity"

## Emitted once when the entity root enters the live scene tree.
signal owner_tree_entered

## Emitted by [SpawnerComponent] before it bakes its replication config.
## Siblings handle this by calling
## [method SpawnerComponent.add_spawn_property] on [param spawner].
signal collecting_spawn_properties(spawner: SpawnerComponent)

## Emitted by [SaveComponent] before it finalizes its tracked properties.
## Siblings handle this by calling
## [method SaveComponent.add_save_property] on [param save].
signal collecting_save_properties(save: SaveComponent)

## Emitted by [SpawnerComponent] after authority and identity are resolved
## but before scene registration. Siblings react with hydration and
## other spawn-time work.
signal spawning

## Emitted by [SpawnerComponent] after scene registration completes.
signal spawned


var _owner: Node
var _spawner_ref: WeakRef
var _save_ref: WeakRef
var _tree_entered_fired: bool = false


## Returns the [NetwEntity] associated with [param node]'s entity root,
## creating one on first access. Returns [code]null[/code] only if
## [param node] is itself invalid.
##
## Resolution rule: [member Node.owner] when in-tree, else walk
## [method Node.get_parent] until null.
static func of(node: Node) -> NetwEntity:
	if not is_instance_valid(node):
		return null
	var root := _find_root(node)
	if not is_instance_valid(root):
		return null
	if root.has_meta(META_KEY):
		return root.get_meta(META_KEY)
	var e := NetwEntity.new()
	e._attach_to(root)
	return e


## Walks to the entity root for [param node]. Uses [member Node.owner]
## when [param node] is in-tree, otherwise the parent chain (which works
## on orphans, where [member Node.owner] is [code]null[/code]).
static func _find_root(node: Node) -> Node:
	if node.owner != null:
		return node.owner
	var n := node
	while n.get_parent() != null:
		n = n.get_parent()
	if n is Window:
		pass
	return n


func _attach_to(root: Node) -> void:
	_owner = root
	root.set_meta(META_KEY, self)
	if root.is_inside_tree():
		_handle_tree_entered.call_deferred()
	else:
		root.tree_entered.connect(_handle_tree_entered, CONNECT_ONE_SHOT)


func _handle_tree_entered() -> void:
	if _tree_entered_fired:
		return
	_tree_entered_fired = true
	owner_tree_entered.emit()


## Returns the entity root (the node that hosts this [NetwEntity] as
## metadata).
func get_owner_node() -> Node:
	return _owner


## Returns [code]true[/code] once [signal owner_tree_entered] has fired
## for this entity.
func has_entered_tree() -> bool:
	return _tree_entered_fired


## Registers [param spawner] as the [SpawnerComponent] for this entity.
## Called by [SpawnerComponent] from
## [constant Node.NOTIFICATION_PARENTED].
func set_spawner(spawner: SpawnerComponent) -> void:
	_spawner_ref = weakref(spawner)


## Returns the registered [SpawnerComponent], or [code]null[/code].
func get_spawner() -> SpawnerComponent:
	return _spawner_ref.get_ref() as SpawnerComponent if _spawner_ref else null


## Registers [param save] as the [SaveComponent] for this entity.
## Called by [SaveComponent] from
## [constant Node.NOTIFICATION_PARENTED].
func set_save(save: SaveComponent) -> void:
	_save_ref = weakref(save)


## Returns the registered [SaveComponent], or [code]null[/code].
func get_save() -> SaveComponent:
	return _save_ref.get_ref() as SaveComponent if _save_ref else null
