## Bundles spawn lifecycle signals, identity tracking, and property
## contributions for one entity root.
##
## A [NetwEntity] is attached as metadata on the owner node that hosts
## [SpawnerComponent] and [SaveComponent]. Access it from any node inside
## the entity with [code]Netw.ctx(self).entity[/code].
##
## [br][br]
## [member identity_id] is the display / save / debug label for the
## entity (e.g. a username). [member represented_peer_id] is the peer
## this entity belongs to -- it drives [member MultiplayerTree.local_player],
## auto-despawn on disconnect, and peer-specific identity lookups.
## [member scene_peer_id] controls which peer gate the entity uses for
## [SceneSynchronizer] visibility; it defaults to
## [method Node.get_multiplayer_authority] when [code]0[/code].
##
## [br][br]
## The spawn phase (driven by [SpawnerComponent]) emits
## [signal spawning] before scene registration and [signal spawned]
## after. Siblings use [method contribute_spawn_property] from
## [constant Node.NOTIFICATION_PARENTED] to inject spawn-synced paths.
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

## Emitted once when the entity root enters the live scene tree.
signal owner_tree_entered

## Emitted by [SpawnerComponent] after authority and identity are resolved
## but before scene registration. Siblings react with hydration and
## other spawn-time work.
signal spawning

## Emitted by [SpawnerComponent] after scene registration completes.
signal spawned


var owner: Node
## Display / save / debug label for this entity (e.g. a username).
## Empty means callers should fall back to their legacy identity source.
## Set by [member SpawnerComponent.identity_id] via
## [method SpawnerComponent._sync_entity_identity].
var identity_id: StringName = &""

## Peer this entity belongs to. Drives [member MultiplayerTree.local_player],
## auto-despawn on disconnect, and [method MultiplayerScene.register_player]
## registration. [code]0[/code] for non-player entities and server-owned
## world objects.
var represented_peer_id := 0

## Peer whose connection gate this entity uses for
## [SceneSynchronizer] visibility. When [code]0[/code], reads
## [method Node.get_multiplayer_authority] from [member owner].
var scene_peer_id := 0:
	get:
		if scene_peer_id != 0:
			return scene_peer_id
		return owner.get_multiplayer_authority() if owner else 0
	set(value):
		scene_peer_id = value
var _spawner_ref: WeakRef
var _save_ref: WeakRef
var _tree_entered_fired: bool = false
var _pending_spawn_props: Array[NodePath] = []
var _pending_save_props: Array = []


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
	if node.has_meta(META_KEY):
		return node
	if node.owner != null:
		return node.owner
	var n := node
	while n.get_parent() != null:
		n = n.get_parent()
		if n.has_meta(META_KEY):
			return n
	if n is Window:
		pass
	return n


func _attach_to(root: Node) -> void:
	owner = root
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


## Returns [code]true[/code] once [signal owner_tree_entered] has fired
## for this entity.
func has_entered_tree() -> bool:
	return _tree_entered_fired


## Registers [param spawner] as the [SpawnerComponent] for this entity
## and flushes any spawn-property contributions buffered before the
## spawner was registered. Called by [SpawnerComponent] from
## [constant Node.NOTIFICATION_PARENTED].
func set_spawner(spawner: SpawnerComponent) -> void:
	_spawner_ref = weakref(spawner)
	for path in _pending_spawn_props:
		spawner.add_spawn_property(path)
	_pending_spawn_props.clear()


## Returns the registered [SpawnerComponent], or [code]null[/code].
func get_spawner() -> SpawnerComponent:
	return _spawner_ref.get_ref() as SpawnerComponent if _spawner_ref else null


## Registers [param save] as the [SaveComponent] for this entity and
## flushes any save-property contributions buffered before it
## registered. Called by [SaveComponent] from
## [constant Node.NOTIFICATION_PARENTED].
func set_save(save: SaveComponent) -> void:
	_save_ref = weakref(save)
	for c in _pending_save_props:
		save.add_save_property(c[0], c[1], c[2], c[3], c[4])
	_pending_save_props.clear()


## Returns the registered [SaveComponent], or [code]null[/code].
func get_save() -> SaveComponent:
	return _save_ref.get_ref() as SaveComponent if _save_ref else null


## Contributes [param path] to the [SpawnerComponent]'s spawn-property
## list. If the spawner is already registered, forwarded immediately;
## otherwise buffered and flushed in [method set_spawner]. Either way,
## contributions made from [constant Node.NOTIFICATION_PARENTED] land
## before Godot's spawn-decode reads
## [member MultiplayerSynchronizer.replication_config].
func contribute_spawn_property(path: NodePath) -> void:
	var spawner := get_spawner()
	if spawner:
		spawner.add_spawn_property(path)
		return
	if path not in _pending_spawn_props:
		_pending_spawn_props.append(path)


## Contributes a tracked property to the [SaveComponent]. Same buffer/
## forward semantics as [method contribute_spawn_property].
func contribute_save_property(
		virtual_name: StringName,
		real_path: NodePath,
		mode: SceneReplicationConfig.ReplicationMode = SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	var save := get_save()
	if save:
		save.add_save_property(virtual_name, real_path, mode, spawn, watch)
		return
	_pending_save_props.append([virtual_name, real_path, mode, spawn, watch])
