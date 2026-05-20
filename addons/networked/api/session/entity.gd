## Runtime identity record for one networked entity root.
##
## [SpawnerComponent] creates and drives the entity lifecycle; sibling
## components use [NetwEntity] to share identity, spawn properties,
## save properties, and interest signals without hard dependencies on
## each other.
##
## [br][br]
## The entity root is the node carrying [constant META_KEY]. In packed
## entity scenes this is normally the scene root. Siblings should access
## it through [code]Netw.ctx(self).entity[/code] or [method of].
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         var entity := Netw.ctx(self).entity
##         entity.contribute_spawn_property(NodePath("..:health"))
##         entity.spawning.connect(_on_spawning)
##
## func _on_spawning() -> void:
##     if multiplayer.is_server():
##         restore_saved_state()
## [/codeblock]
class_name NetwEntity
extends RefCounted

const META_KEY := &"netw_entity"

## Semantic ownership marker derived from [member peer_id].
enum Ownership { PEER, SERVER }

## Emitted once when the entity root enters the live scene tree.
signal owner_tree_entered

## Emitted before scene registration after identity is resolved.
signal spawning

## Emitted by [SpawnerComponent] after scene registration completes.
signal spawned

## Emitted when this entity becomes visible to [param peer_id].
##
## On the server, [param peer_id] is the observer. On a client, it is
## the local peer and means this entity's synchronizers became visible.
## Prefer [signal NetwInterestLayer.entity_visible] when client code
## needs the layer that caused the transition.
signal interest_enter(peer_id: int)

## Emitted when this entity stops being visible to [param peer_id].
signal interest_exit(peer_id: int)

## Emitted on the owner client when another peer [param peer_id]
## gains visibility of this entity through [param layer_id].
##
## Use this for owner-side UI such as "who can see me?" indicators.
## Requires [member InterestComponent.report_observers] on the server.
signal observer_entered(layer_id: StringName, peer_id: int)

## Emitted on the owner client when [param peer_id] stops observing
## this entity through [param layer_id].
signal observer_left(layer_id: StringName, peer_id: int)

## [member Node.owner] that holds this entity.
var owner: Node
## Stable display/save/debug label for this entity.
var entity_id: StringName = &""

## Joined peer this entity represents.
##
## [code]0[/code] means a server-owned world entity. Non-zero values
## drive player registration, local-player tracking, and disconnect
## despawn.
var peer_id := 0

## Derived from [member peer_id]. See [enum Ownership].
var ownership: Ownership:
	get: return Ownership.PEER if peer_id != 0 else Ownership.SERVER

## Returns [member SpawnerComponent.is_template] for this entity's
## registered spawner.
var is_template: bool:
	get:
		var spawner := get_spawner()
		return spawner.is_template if spawner else false

var _spawner_ref: WeakRef
var _save_ref: WeakRef
var _tree_entered_fired: bool = false
var _owner_exiting_tree: bool = false
var _pending_spawn_props: Array[NodePath] = []
var _pending_save_props: Array = []

var _synchronizers_cache: Array[MultiplayerSynchronizer] = []
var _synchronizers_dirty: bool = true
var _parent_entity_resolved: bool = false
var _parent_entity_ref: WeakRef


## Returns the [NetwEntity] associated with [param node]'s entity root.
##
## Creates the record on first access. Set [member Node.owner] or attach
## metadata first when a runtime-built subtree has an ambiguous root.
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


## Returns the entity id from a [code]entity_id|peer_id[/code] name.
static func parse_entity(node_name: String) -> StringName:
	var parts := node_name.split("|")
	if parts.size() != 2:
		return &""
	if parts[0].is_empty():
		return &""
	return StringName(parts[0])


## Returns the peer id from a [code]entity_id|peer_id[/code] name.
static func parse_peer(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0


## Formats [param entity_id] and [param peer_id] as a node name.
static func format_name(entity_id: String, peer_id: int) -> String:
	return "%s|%d" % [entity_id, peer_id]


## Encodes identity into [param node] and its [NetwEntity].
##
## [codeblock]
## var player := NetwEntity.bundle(copy, peer_id, username)
## scene.add_player(player)
## [/codeblock]
static func bundle(
		node: Node,
		peer_id: int,
		entity_id: StringName,
) -> Node:
	node.name = format_name(str(entity_id), peer_id)
	var entity := of(node)
	if entity:
		entity.entity_id = entity_id
		entity.peer_id = peer_id
	var spawner := SpawnerComponent.unwrap(node)
	if spawner:
		spawner.entity_id = entity_id
		spawner.peer_id = peer_id
	return node


## Walks to the entity root for [param node].
static func _find_root(node: Node) -> Node:
	if node.has_meta(META_KEY):
		return node
	if node.owner != null:
		return node.owner
	var n := node
	while n.get_parent() != null:
		n = n.get_parent()
		if n.has_meta(META_KEY):
			return n
	if n != node:
		Netw.dbg.trace(
				("NetwEntity.of: walked from '%s' up to topmost "
				+ "ancestor '%s' with no META and no Node.owner; "
				+ "attaching entity to '%s'. Set Node.owner or "
				+ "pre-attach META on the intended root to "
				+ "disambiguate."), [node.name, n.name, n.name])
	return n


func _attach_to(root: Node) -> void:
	owner = root
	root.set_meta(META_KEY, self)
	if not root.tree_entered.is_connected(_handle_tree_entered):
		root.tree_entered.connect(_handle_tree_entered)
	if not root.tree_exiting.is_connected(_handle_tree_exiting):
		root.tree_exiting.connect(_handle_tree_exiting)
	if root.is_inside_tree():
		_handle_tree_entered.call_deferred()


func _handle_tree_entered() -> void:
	_owner_exiting_tree = false
	if _tree_entered_fired:
		return
	_tree_entered_fired = true
	owner_tree_entered.emit()


func _handle_tree_exiting() -> void:
	_owner_exiting_tree = true


## Returns [code]true[/code] once [signal owner_tree_entered] has fired
## for this entity.
func has_entered_tree() -> bool:
	return _tree_entered_fired


## Registers this entity's [SpawnerComponent].
##
## Buffered spawn-property contributions are flushed immediately.
func set_spawner(spawner: SpawnerComponent) -> void:
	_spawner_ref = weakref(spawner)
	for path in _pending_spawn_props:
		spawner.add_spawn_property(path)
	_pending_spawn_props.clear()


## Returns the registered [SpawnerComponent], or [code]null[/code].
func get_spawner() -> SpawnerComponent:
	return _spawner_ref.get_ref() as SpawnerComponent if _spawner_ref else null


## Registers this entity's [SaveComponent].
##
## Buffered save-property contributions are flushed immediately.
func set_save(save: SaveComponent) -> void:
	_save_ref = weakref(save)
	for c in _pending_save_props:
		save.add_save_property(c[0], c[1], c[2], c[3], c[4])
	_pending_save_props.clear()


## Returns the registered [SaveComponent], or [code]null[/code].
func get_save() -> SaveComponent:
	return _save_ref.get_ref() as SaveComponent if _save_ref else null


## Adds [param path] to the entity's spawn packet.
##
## Call from [constant Node.NOTIFICATION_PARENTED] so the property lands
## before Godot decodes the spawn packet.
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         Netw.ctx(self).entity.contribute_spawn_property(
##             NodePath("..:health")
##         )
## [/codeblock]
func contribute_spawn_property(path: NodePath) -> void:
	var spawner := get_spawner()
	if spawner:
		spawner.add_spawn_property(path)
		return
	if path not in _pending_spawn_props:
		_pending_spawn_props.append(path)


## Adds a property to the entity's save component.
##
## Calls before [SaveComponent] registers are buffered.
func contribute_save_property(
		virtual_name: StringName,
		real_path: NodePath,
		mode: SceneReplicationConfig.ReplicationMode =
				SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	var save := get_save()
	if save:
		save.add_save_property(virtual_name, real_path, mode, spawn, watch)
		return
	_pending_save_props.append([virtual_name, real_path, mode, spawn, watch])


# ---------------------------------------------------------------------------
# Synchronizer wiring
# ---------------------------------------------------------------------------

## Returns synchronizers targeting this entity root.
func synchronizers() -> Array[MultiplayerSynchronizer]:
	if _synchronizers_dirty or _synchronizers_cache.is_empty():
		if is_instance_valid(owner):
			var found := SynchronizersCache.get_synchronizers(owner)
			if not found.is_empty() or not _owner_exiting_tree:
				_synchronizers_cache = found
			_synchronizers_dirty = _owner_exiting_tree \
					and _synchronizers_cache.is_empty()
	return _synchronizers_cache


## Invalidates the cached synchronizer list so the next call to
## [method synchronizers] re-scans the entity root.
func invalidate_synchronizers_cache() -> void:
	_synchronizers_dirty = true
	if is_instance_valid(owner):
		SynchronizersCache.clear_cache(owner)


## Returns the nearest ancestor [NetwEntity], or [code]null[/code].
func parent_entity() -> NetwEntity:
	if not _parent_entity_resolved:
		_parent_entity_resolved = true
		var found := _walk_for_parent_entity()
		_parent_entity_ref = weakref(found) if found else null
	if not _parent_entity_ref:
		return null
	var parent := _parent_entity_ref.get_ref() as NetwEntity
	if parent and not is_instance_valid(parent.owner):
		_parent_entity_ref = null
		return null
	return parent


func _walk_for_parent_entity() -> NetwEntity:
	if not is_instance_valid(owner):
		return null
	var n := owner.get_parent()
	while is_instance_valid(n):
		if n.has_meta(META_KEY):
			return n.get_meta(META_KEY) as NetwEntity
		n = n.get_parent()
	return null
