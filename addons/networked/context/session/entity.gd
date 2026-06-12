## Runtime identity record for one networked entity root, player or
## server-owned.
##
## [MultiplayerEntity] creates and drives the entity lifecycle. Sibling
## components use [NetwEntity] to share identity ([member entity_id],
## [member peer_id]), contribute to the spawn packet
## ([method contribute_spawn_property]) and saved state
## ([method contribute_save_property]), and observe visibility
## ([signal interest_enter], [signal interest_exit]) without hard
## dependencies on each other.
##
## [br][br]
## [member peer_id] classifies the entity. A non-zero value is a player and
## names the peer it represents. [code]0[/code] is a server-owned entity such
## as an NPC or world object. See [member is_player] and [enum Ownership].
##
## [br][br]
## The entity root is the node representing the networked entity (typically
## the scene root in packed entity scenes). Siblings should access
## it through [code]Netw.ctx(self).entity[/code] (see [member NetwContext.entity] 
## or [method of].
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         var entity := Netw.ctx(self).entity
##         entity.contribute_spawn_property(self, &"health")
##         entity.spawning.connect(_on_spawning)
##
## func _on_spawning() -> void:
##     if multiplayer.is_server():
##         restore_saved_state()
## [/codeblock]
class_name NetwEntity
extends RefCounted

# Metadata key that stores the [NetwEntity] record on an entity root.
const _META_KEY := &"netw_entity"

# Reserved key for Networked identity inside custom spawn dictionaries.
# User payload remains at the top level. [method decorate_spawn] writes this
# key, and [method spawn_identity] reads it back before [method bind].
const _SPAWN_NETW_KEY := "_netw"

## Whether an entity represents a joined peer or the server, derived from
## [member peer_id].
enum Ownership {
	## Represents a joined peer. A player. [member peer_id] is non-zero.
	PEER,
	## Server-owned entity such as an NPC or world object. [member peer_id]
	## is [code]0[/code].
	SERVER,
}

## Whether an entity is server controlled or peer controlled, derived from
## [member controller].
enum ControlKind {
	## A peer currently controls the entity. [member controller] is non-zero.
	PEER,
	## Server controlled entity. [member controller] is [code]0[/code].
	SERVER,
}

## Key roles identifying generic component slots on this entity record.
enum Slot {
	## Backup store slot for the entity's [SaveComponent].
	SAVE,
	## Orchestration slot for the entity's [MultiplayerEntity].
	MULTIPLAYER_ENTITY,
	## Ancestor visibility gate slot.
	INTEREST_GATE,
}


# Buffered proxy-style property contribution.
#
# Keeps [method contribute_save_property] calls typed while the destination
# [ProxySynchronizer] registers later during packed-scene construction.
class _PropertyContribution extends RefCounted:
	var source: Node
	var virtual_name: StringName
	var property: StringName
	var mode: SceneReplicationConfig.ReplicationMode
	var spawn: bool
	var watch: bool


	func _init(
			p_source: Node,
			p_virtual_name: StringName,
			p_property: StringName,
			p_mode: SceneReplicationConfig.ReplicationMode,
			p_spawn: bool,
			p_watch: bool,
	) -> void:
		source = p_source
		virtual_name = p_virtual_name
		property = p_property
		mode = p_mode
		spawn = p_spawn
		watch = p_watch


	func matches(
			p_virtual_name: StringName,
			p_source: Node,
			p_property: StringName,
	) -> bool:
		return (
				virtual_name == p_virtual_name
				and source == p_source
				and property == p_property
		)


	func register_with(proxy: ProxySynchronizer) -> void:
		proxy.register_node_property(
			virtual_name,
			source,
			property,
			mode,
			spawn,
			watch,
		)


# Buffered spawn-property contribution.
class _SpawnContribution extends RefCounted:
	var source: Node
	var property: StringName


	func _init(p_source: Node, p_property: StringName) -> void:
		source = p_source
		property = p_property

## Emitted once when the entity root enters the live scene tree.
signal owner_tree_entered

## Emitted before scene registration after identity is resolved.
signal spawning

## Emitted by [MultiplayerEntity] after scene registration completes.
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

## Emitted when [member controller] changes.
signal control_changed(previous_peer: int, peer: int)

## [member Node.owner] that holds this entity.
var owner: Node
## Stable display/save/debug label for this entity.
var entity_id: StringName = &""

## Peer id of the joined player this entity represents (the same id carried by
## [member ResolvedJoin.peer_id]), or [code]0[/code] for a server-owned entity
## (NPC, prop, world object).
##
## A non-zero value drives [method MultiplayerScene.register_player],
## [member MultiplayerTree.local_player] tracking, and an automatic
## [method MultiplayerEntity.despawn] when its peer disconnects. This is the
## source of the player test. See [member is_player].
var peer_id := 0

var _pending_controller := 0

## Peer that currently steers this entity. [code]0[/code] means the server.
##
## [MultiplayerEntity] applies [member controller] to
## [method Node.set_multiplayer_authority]. Setting this property on a live
## entity routes through [method MultiplayerEntity.set_controller].
## [codeblock]
## var entity := NetwEntity.of(ball)
## if entity.control_kind == NetwEntity.ControlKind.PEER:
##     show_controller(entity.controller_participant)
## [/codeblock]
var controller: int:
	get:
		var entity := multiplayer_entity
		if entity:
			return entity.controller
		return _pending_controller
	set(value):
		var entity := multiplayer_entity
		if entity:
			entity.set_controller(value)
		else:
			_set_controller_value(value)

## Derived from [member controller]. See [enum ControlKind].
var control_kind: ControlKind:
	get:
		return ControlKind.PEER if controller != 0 else ControlKind.SERVER

## [code]true[/code] when the local peer controls this entity.
var is_controlled_locally: bool:
	get:
		if controller == 0 or not is_instance_valid(owner):
			return false
		if not owner.multiplayer or owner.multiplayer.multiplayer_peer == null:
			return false
		return controller == owner.multiplayer.get_unique_id()

## Joined player record for [member controller], rebuilt from the local
## [MultiplayerTree] roster. Never serialized.
var controller_participant: ResolvedJoin:
	get:
		if controller == 0 or not is_instance_valid(owner):
			return null
		var mt := MultiplayerTree.resolve(owner)
		return mt.get_joined_player(controller) if mt else null

## Derived from [member peer_id]. See [enum Ownership].
var ownership: Ownership:
	get:
		return Ownership.PEER if peer_id != 0 else Ownership.SERVER

## [code]true[/code] when this entity represents a joined player rather than a
## server-owned entity. The canonical player test across the addon. Equivalent
## to [code]ownership == Ownership.PEER[/code] and to a non-zero
## [member peer_id].
## [codeblock]
## # A projectile hit some entity; only react if it was a player.
## var entity := NetwEntity.of(hit_node)
## if entity and entity.is_player:
##     eliminate_player(entity.peer_id)
## [/codeblock]
var is_player: bool:
	get:
		return peer_id != 0

## Returns [member MultiplayerEntity.is_template] for this entity's
## registered spawner.
var is_template: bool:
	get:
		var entity := multiplayer_entity
		return entity.is_template if entity else false

var _slots: Dictionary[int, WeakRef] = { }
var _slot_requires: Dictionary[int, Array] = { }
var _tree_entered_fired: bool = false
var _owner_exiting_tree: bool = false
var _pending_spawn_props: Array[_SpawnContribution] = []
var _pending_save_props: Array[_PropertyContribution] = []

var _synchronizers_cache: Array[MultiplayerSynchronizer] = []
var _synchronizers_dirty: bool = true
var _parent_entity_resolved: bool = false
var _parent_entity_ref: WeakRef


## Returns the [NetwEntity] associated with [param node]'s entity root.
##
## Walks the parent chain to locate the entity root. Returns [code]null[/code]
## if not found.
static func of(node: Node) -> NetwEntity:
	if not is_instance_valid(node):
		return null
	var n := node
	while n != null:
		if n.has_meta(_META_KEY):
			return n.get_meta(_META_KEY) as NetwEntity
		n = n.get_parent()
	return null


## Force get-or-create [NetwEntity] on the specific [param root] node.
##
## Attaches the [NetwEntity] to [param root] as its entity root, even if
## [param root] has an ambiguous owner or parent.
static func ensure(root: Node) -> NetwEntity:
	if not is_instance_valid(root):
		return null
	if root.has_meta(_META_KEY):
		return root.get_meta(_META_KEY) as NetwEntity
	var e := NetwEntity.new()
	e._attach_to(root)
	return e


## Climbs parent chain to topmost orphan during instantiation to get-or-create;
## falls back to lookup-only once in-tree.
static func resolve(node: Node) -> NetwEntity:
	if not is_instance_valid(node):
		return null

	var existing := of(node)
	if existing:
		return existing

	if node.is_inside_tree():
		return null

	var root := node
	while root.get_parent() != null:
		if root.get_parent().is_inside_tree():
			return null
		root = root.get_parent()
	return ensure(root)


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


# Formats entity_id and peer_id as a node name.
static func _format_name(entity_id: String, peer_id: int) -> String:
	return "%s|%d" % [entity_id, peer_id]


## Returns the node name for the player represented by [param rj].
static func name_for(rj: ResolvedJoin) -> String:
	return _format_name(rj.username, rj.peer_id)


## Returns the player node associated with [param rj] under [param root],
## or [code]null[/code] if not found.
static func find(root: Node, rj: ResolvedJoin) -> Node:
	if not is_instance_valid(root) or rj == null:
		return null
	return root.get_node_or_null(name_for(rj))


## Binds [param entity_id] and [param peer_id] onto [param node].
##
## This is the public identity binding surface. Once bound, the node's name
## is owned by the network synchronization system and must not be modified.
## [codeblock]
## var player := NetwEntity.bind(copy, username, peer_id)
## scene.add_player(player)
## [/codeblock]
static func bind(
		node: Node,
		entity_id: StringName,
		peer_id: int,
) -> Node:
	node.name = _format_name(str(entity_id), peer_id)
	var entity := ensure(node)
	if entity:
		entity.entity_id = entity_id
		entity.peer_id = peer_id
	return node


## Decodes bindable identity from custom spawn data.
##
## Use the original spawn [Dictionary] for gameplay data. This method only
## extracts the identity needed to call [method NetwEntity.SpawnIdentity.bind].
## [codeblock]
## # Server:
## var data := NetwEntity.decorate_spawn(
##     {spawn_index = index},
##     resolved_join
## )
## spawner.spawn(data)
##
## #             |
## #             v (Network spawn replication)
## #             |
##
## # Client (spawn_function):
## func _custom_spawn(data: Dictionary) -> Node:
##     var spawn_identity := NetwEntity.spawn_identity(data)
##
##     var player := PLAYER.instantiate()
##     spawn_identity.bind(player)
##
##     player.spawn_index = data.spawn_index
##     return player
## [/codeblock]
static func spawn_identity(data: Dictionary) -> SpawnIdentity:
	return SpawnIdentity.new(data)


## Deprecated compatibility alias for [method spawn_identity].
static func spawn(data: Dictionary) -> SpawnIdentity:
	return spawn_identity(data)


## Returns [param data] with Networked spawn identity attached.
##
## The returned [Dictionary] is a duplicate. The input [param data] is not
## mutated. [code]_netw[/code] is reserved and must not already be present.
static func decorate_spawn(
		data: Dictionary,
		rj: ResolvedJoin,
) -> Dictionary:
	assert(
		not data.has(_SPAWN_NETW_KEY),
		"NetwEntity.decorate_spawn: '_netw' is reserved.",
	)
	var out := data.duplicate(true)
	out[_SPAWN_NETW_KEY] = {
		"entity_id": rj.username,
		"peer_id": rj.peer_id,
	}
	return out


## Bindable identity decoded from custom spawn data.
class SpawnIdentity extends RefCounted:
	## Decoded entity ID for the spawned node, mapped from
	## [member NetwEntity.entity_id].
	var entity_id: StringName = &""
	## Decoded peer ID for the spawned node, mapped from
	## [member NetwEntity.peer_id].
	var peer_id: int = 0


	func _init(spawn_data: Dictionary) -> void:
		var netw: Dictionary = spawn_data.get(_SPAWN_NETW_KEY, { })
		entity_id = StringName(netw.get("entity_id", ""))
		peer_id = int(netw.get("peer_id", 0))


	## Binds this identity onto [param node].
	func bind(node: Node) -> Node:
		return NetwEntity.bind(node, entity_id, peer_id)


## Returns a [NodePath] from [param source] to [param target].
func relative_path(source: Node, target: Node) -> NodePath:
	if not is_instance_valid(source) or not is_instance_valid(target):
		return NodePath("")
	return source.get_path_to(target)


## Returns [param property] on [param source] relative to [param base].
##
## Defaults to the entity root, matching [MultiplayerEntity]'s path space.
func property_path(
		source: Node,
		property: StringName,
		base: Node = null,
) -> NodePath:
	var root := base if base else owner
	if not is_instance_valid(root):
		return NodePath("")
	var rel := relative_path(root, source)
	if rel.is_empty():
		return NodePath("")
	return NodePath("%s:%s" % [rel, property])


func _set_controller_value(value: int) -> void:
	var previous := _pending_controller
	_pending_controller = value
	if previous != value:
		control_changed.emit(previous, value)


func _attach_to(root: Node) -> void:
	owner = root
	root.set_meta(_META_KEY, self)
	if not root.tree_entered.is_connected(_handle_tree_entered):
		root.tree_entered.connect(_handle_tree_entered)
	if not root.tree_exiting.is_connected(_handle_tree_exiting):
		root.tree_exiting.connect(_handle_tree_exiting)
	if root.is_inside_tree():
		_handle_tree_entered.call_deferred()


func _handle_tree_entered() -> void:
	_owner_exiting_tree = false
	_parent_entity_resolved = false
	_parent_entity_ref = null

	if _tree_entered_fired:
		return
	_tree_entered_fired = true
	owner_tree_entered.emit()

	if multiplayer_entity == null:
		var parent := parent_entity()
		if parent:
			for c in _pending_spawn_props:
				parent.contribute_spawn_property(c.source, c.property)
			_pending_spawn_props.clear()
			if _slot_requires.has(Slot.MULTIPLAYER_ENTITY):
				_slot_requires[Slot.MULTIPLAYER_ENTITY].clear()

			for c in _pending_save_props:
				parent.contribute_save_property(
					c.source,
					c.virtual_name,
					c.property,
					c.mode,
					c.spawn,
					c.watch,
				)
			_pending_save_props.clear()
			if _slot_requires.has(Slot.SAVE):
				_slot_requires[Slot.SAVE].clear()


func _handle_tree_exiting() -> void:
	_owner_exiting_tree = true


## Returns [code]true[/code] once [signal owner_tree_entered] has fired
## for this entity.
func has_entered_tree() -> bool:
	return _tree_entered_fired


## Associate [param component] with [param slot_id] on this entity record.
##
## Clears the slot reference when [param component] is [code]null[/code].
## Runs any pending consumers queued via [method require] immediately.
func provide(slot_id: int, component: Object) -> void:
	if component == null:
		_slots.erase(slot_id)
		return
	_slots[slot_id] = weakref(component)
	if _slot_requires.has(slot_id):
		var list: Array = _slot_requires[slot_id]
		var consumers := list.duplicate()
		list.clear()
		for consumer in consumers:
			if (consumer as Callable).is_valid():
				(consumer as Callable).call(component)


## Request the component from [param slot_id], executing [param consumer] once available.
##
## Runs [param consumer] immediately if the component is already present.
func require(slot_id: int, consumer: Callable) -> void:
	var component := slot(slot_id)
	if component:
		consumer.call(component)
		return
	if not _slot_requires.has(slot_id):
		_slot_requires[slot_id] = []
	_slot_requires[slot_id].append(consumer)


## Returns the component bound to [param slot_id], or [code]null[/code] if missing.
##
## Evicts dead weak references automatically.
func slot(slot_id: int) -> Object:
	if _slots.has(slot_id):
		var wr: WeakRef = _slots[slot_id]
		var ref := wr.get_ref()
		if ref != null:
			return ref
		else:
			_slots.erase(slot_id)
	return null

## The entity's [SaveComponent] slot, if provided.
var save: SaveComponent:
	get:
		return slot(Slot.SAVE) as SaveComponent
	set(value):
		provide(Slot.SAVE, value)
		_pending_save_props.clear()

## The entity's [MultiplayerEntity] slot, if provided.
var multiplayer_entity: MultiplayerEntity:
	get:
		return slot(Slot.MULTIPLAYER_ENTITY) as MultiplayerEntity
	set(value):
		provide(Slot.MULTIPLAYER_ENTITY, value)
		_pending_spawn_props.clear()


## Adds [param property] from [param source] to the entity's spawn packet.
##
## Call from [constant Node.NOTIFICATION_PARENTED] so the property lands
## before Godot decodes the spawn packet. The path is resolved relative to
## the entity root, so components do not need to account for scene nesting.
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         Netw.ctx(self).entity.contribute_spawn_property(
##             self,
##             &"health"
##         )
## [/codeblock]
func contribute_spawn_property(source: Node, property: StringName) -> void:
	var mp_ent := multiplayer_entity
	if mp_ent:
		var path := property_path(source, property)
		if not path.is_empty():
			mp_ent.add_spawn_property(path)
		return

	for c in _pending_spawn_props:
		if c.source == source and c.property == property:
			return

	var contribution := _SpawnContribution.new(source, property)
	_pending_spawn_props.append(contribution)

	require(
		Slot.MULTIPLAYER_ENTITY,
		func(ent: MultiplayerEntity) -> void:
			var path := property_path(contribution.source, contribution.property)
			if not path.is_empty():
				ent.add_spawn_property(path)
	)


## Adds a property to the entity's save component.
##
## Calls before [SaveComponent] registers are buffered.
func contribute_save_property(
		source: Node,
		virtual_name: StringName,
		property: StringName,
		mode: SceneReplicationConfig.ReplicationMode = SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	var save_comp := save
	if save_comp:
		var contribution := _PropertyContribution.new(
			source,
			virtual_name,
			property,
			mode,
			spawn,
			watch,
		)
		contribution.register_with(save_comp)
		return
	if _has_pending_save_property(virtual_name, source, property):
		return
	var contribution := _PropertyContribution.new(
		source,
		virtual_name,
		property,
		mode,
		spawn,
		watch,
	)
	_pending_save_props.append(contribution)

	require(
		Slot.SAVE,
		func(s: SaveComponent) -> void:
			contribution.register_with(s)
	)


func _has_pending_save_property(
		virtual_name: StringName,
		source: Node,
		property: StringName,
) -> bool:
	for contribution in _pending_save_props:
		if contribution.matches(virtual_name, source, property):
			return true
	return false

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
		if n.has_meta(_META_KEY):
			return n.get_meta(_META_KEY) as NetwEntity
		n = n.get_parent()
	return null
