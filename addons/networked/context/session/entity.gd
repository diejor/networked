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
## The entity root is the node representing the networked entity. Siblings can
## connect to [signal spawning] or [signal spawned] from the editor when they
## only need decoded identity. Use [constant Node.NOTIFICATION_PARENTED] only
## when the component must also call [method contribute_spawn_property] before
## Godot reads [member MultiplayerEntity.replication_config].
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
## [br][br]
## A spawned [Node] with [MultiplayerEntity] must bind [member entity_id] and
## [member peer_id] before it enters the tree. Helper pairs make that contract
## explicit. Manual paths must perform the same bind.
## [br][br]
## Use [method wrap_spawn] inside [member MultiplayerSpawner.spawn_function]
## when [method spawn_for] owns the spawn call.
## [codeblock]
## spawner.spawn_function = NetwEntity.wrap_spawn(_spawn_player)
## NetwEntity.spawn_for(spawner, participant, payload)
## [/codeblock]
## Use [method decorate_spawn] when [member MultiplayerSpawner.spawn_function]
## still uses [method wrap_spawn], but gameplay code owns when
## [method MultiplayerSpawner.spawn] runs.
## [codeblock]
## spawner.spawn_function = NetwEntity.wrap_spawn(_spawn_player)
## spawner.spawn(NetwEntity.decorate_spawn(payload, join))
## [/codeblock]
## Use [method spawn_identity] when a raw
## [member MultiplayerSpawner.spawn_function] owns identity binding itself.
## [codeblock]
## var identity := NetwEntity.spawn_identity(data)
## identity.bind(node)
## [/codeblock]
## Use [method bind] before [method Node.add_child] when identity rides the
## [member Node.name] channel instead of custom spawn data.
## [codeblock]
## NetwEntity.bind(node, entity_id, peer_id)
## parent.add_child(node)
## [/codeblock]
##
## A [MultiplayerScene] with [member MultiplayerScene.gate] requires each
## spawned [Node] to own its [NetwEntity] record. A plain [Node] satisfies that
## invariant with [method ensure] before [method MultiplayerScene.track_node].
class_name NetwEntity
extends RefCounted

# Metadata key that stores the [NetwEntity] record on an entity root.
const _META_KEY := &"netw_entity"

# Reserved keys for Networked spawn identity envelopes.
#
# [method spawn_for] owns the outer Variant as a Dictionary with these keys.
# [method wrap_spawn] unwraps it and passes the user payload through unchanged.
# [method decorate_spawn] remains the low-level Dictionary path.
const _SPAWN_NETW_KEY := "_netw"
const _SPAWN_DATA_KEY := "data"

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
## [member controller]. See [enum ControlKind].
enum ControlKind {
	## A peer currently controls the entity. [member controller] is non-zero.
	PEER_CONTROLLED,
	## Server controlled entity. [member controller] is [code]0[/code].
	SERVER_CONTROLLED,
}

## Key roles identifying generic component slots on this entity record.
enum Slot {
	## Backup store slot for the entity's [SaveComponent].
	SAVE,
	## Orchestration slot for the entity's [MultiplayerEntity].
	MULTIPLAYER_ENTITY,
	## Ancestor visibility gate slot.
	INTEREST_GATE,
	## Server-authoritative state slot for the entity's [StateSynchronizer].
	STATE,
	## Controller-authoritative input slot for the entity's [InputSynchronizer].
	INPUT,
	## Per-entity tick-keyed [NetwTimeline] of state and input snapshots.
	TIMELINE,
	## Prediction and reconciliation slot, filled by the prediction component.
	PREDICTION,
}


# Buffered proxy-style property contribution.
#
# Keeps [method contribute_save_property] calls typed while the destination
# [ProxySynchronizer] registers later during packed-scene construction.
class _PropertyContribution extends RefCounted:
	var source: Node
	var virtual_name: StringName
	var property: StringName
	var save_mode: SaveComponent.SaveMode
	var interval: float
	var mode: SceneReplicationConfig.ReplicationMode
	var spawn: bool
	var watch: bool


	func _init(
			p_source: Node,
			p_virtual_name: StringName,
			p_property: StringName,
			p_save_mode: SaveComponent.SaveMode,
			p_interval: float,
			p_mode: SceneReplicationConfig.ReplicationMode,
			p_spawn: bool,
			p_watch: bool,
	) -> void:
		source = p_source
		virtual_name = p_virtual_name
		property = p_property
		save_mode = p_save_mode
		interval = p_interval
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


	func register_with(save_comp: SaveComponent) -> void:
		save_comp.add_save_property(
			virtual_name,
			source,
			property,
			save_mode,
			interval,
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

## Emitted once after identity, authority, and spawn-packet properties are
## applied. The owner is in the tree, but [method Node._ready] may still be
## running.
signal spawning

## Emitted once after scene registration and the owner's [method Node._ready]
## complete.
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

## Emitted after [MultiplayerEntity] has authority settled.
##
## Fires on the initial spawn and on every
## [method MultiplayerEntity.reparent_to].
## Components that unregister in [code]_exit_tree[/code] reconnect their runtime
## service registration here so reparenting self-heals without
## [method Node.request_ready].
##
## [param reparent] is [code]null[/code] for a fresh spawn, or the in-flight
## [MultiplayerEntity.ReparentOpts] for a reparent.
signal reparented(reparent: MultiplayerEntity.ReparentOpts)

## Announces that this entity's player is now the locally displayed view.
##
## Driven by the local display ([HostSceneView] on a listen-server host) whenever
## this player's scene becomes the one shown on this peer, on the initial display
## and on every return. Whatever owns the camera reacts here however it wants
## ([method Camera2D.make_current], a custom rig, or a PhantomCamera host
## priority), so the display system never needs to know the camera type.
signal view_activated

## [member Node.owner] that holds this entity.
var owner: Node
## The active [method MultiplayerEntity.reparent_to], or [code]null[/code].
##
## Set before the reparent and cleared after reparenting so
## [code]_exit_tree[/code] consumers can tell a reparent from a despawn.
var reparenting: MultiplayerEntity.ReparentOpts = null
## Stable display/save/debug label for this entity.
var entity_id: StringName = &""

## Peer id of the participant this entity represents, or [code]0[/code] for a
## server-owned entity (NPC, prop, world object).
##
## A non-zero value drives [method MultiplayerScene.register_player],
## [member MultiplayerTree.local_player] tracking, and an automatic
## [method MultiplayerEntity.despawn] when its peer disconnects. This is the
## source of the player test. See [member is_player].
var peer_id := 0

## Compact wire route naming this entity in [LivenessService], or
## [code]0[/code] when unroutable.
##
## Decoded from the [method wrap_spawn] envelope or the [MultiplayerEntity]
## spawn packet. [RelayService] frames carry this value instead of a node
## path, so a packet can always be addressed even while the node it targets is
## still spawning. See [method LivenessService.route_of] for lookups by
## entity and [method LivenessService.entity_of] for the reverse.
var route := 0

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
		return ControlKind.PEER_CONTROLLED if controller != 0 else ControlKind.SERVER_CONTROLLED

## [code]true[/code] when the local peer controls this entity.
var is_controlled_locally: bool:
	get:
		if controller == 0 or not is_instance_valid(owner):
			return false
		if not owner.multiplayer or owner.multiplayer.multiplayer_peer == null:
			return false
		return controller == owner.multiplayer.get_unique_id()

## Participant represented by [member peer_id], or [code]null[/code].
##
## This resolves the live session handle for player avatars. Server-owned
## entities, props, and NPCs return [code]null[/code].
var participant: NetwParticipant:
	get:
		if peer_id == 0 or not is_instance_valid(owner):
			return null
		var mt := MultiplayerTree.resolve(owner)
		return mt.get_participant(peer_id) if mt else null

## Participant steering [member controller], or [code]null[/code].
##
## This is independent from [member participant]. A server-owned entity can be
## controlled by a participant without representing that participant.
var controller_participant: NetwParticipant:
	get:
		if controller == 0 or not is_instance_valid(owner):
			return null
		var mt := MultiplayerTree.resolve(owner)
		return mt.get_participant(controller) if mt else null

## Derived from [member peer_id]. See [enum Ownership].
var ownership: Ownership:
	get:
		return Ownership.PEER if peer_id != 0 else Ownership.SERVER

## [code]true[/code] when this entity represents a participant rather than a
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

var _slots: Dictionary[Slot, WeakRef] = { }
var _slot_requires: Dictionary[Slot, Array] = { }
var _tree_entered_fired: bool = false
var _owner_exiting_tree: bool = false
var _pending_spawn_props: Array[_SpawnContribution] = []
var _pending_save_props: Array[_PropertyContribution] = []

var _synchronizers_cache: Array[MultiplayerSynchronizer] = []
var _synchronizers_dirty: bool = true
var _parent_entity_resolved: bool = false
var _parent_entity_ref: WeakRef

# Component ID mapping (R2 component-ID table)
var _registered_components: Array[Node] = []
var _components_by_id: Dictionary[int, NodePath] = {}
var _ids_by_path: Dictionary[NodePath, int] = {}
var _table_poisoned: bool = false
var _table_hash: int = 0


## Registers a sub-node as a component of the entity for RPC routing.
func register_component(component: Node) -> void:
	if component == owner:
		return
	if not _registered_components.has(component):
		_registered_components.append(component)


## Builds the component ID mapping table and computes its 16-bit hash.
func hydrate_components() -> void:
	var paths: Array[NodePath] = []
	for comp in _registered_components:
		if is_instance_valid(comp):
			var rel := relative_path(owner, comp)
			if not rel.is_empty():
				paths.append(rel)
	# Sort lexicographically to ensure order-insensitivity
	paths.sort()

	_components_by_id.clear()
	_ids_by_path.clear()
	for i in paths.size():
		var idx := i + 1
		if idx >= 255:
			break
		_components_by_id[idx] = paths[i]
		_ids_by_path[paths[i]] = idx

	_table_hash = _compute_table_hash(paths)


# The 16-bit table hash rides the spawn packet so a client can detect a
# structural mismatch with the server and poison its table. It covers the
# component paths (which drive comp ids) and every routed script's sorted
# @rpc method list (which drives 1-byte method ids), so a version skew in
# either falls back to string paths and method names instead of misrouting.
func _compute_table_hash(paths: Array[NodePath]) -> int:
	var s := ""
	for path in paths:
		s += str(path) + ","

	s += "|methods:"
	var scripts: Array[Script] = []
	if is_instance_valid(owner) and owner.get_script():
		scripts.append(owner.get_script())
	for comp in _registered_components:
		if is_instance_valid(comp) and comp.get_script():
			scripts.append(comp.get_script())
	for sc in scripts:
		var methods: Array = []
		var cfg: Dictionary = sc.get_rpc_config()
		if cfg:
			for m in cfg:
				methods.append(String(m))
		methods.sort()
		s += "|" + "/".join(methods)

	return s.hash() & 0xFFFF


func _on_identity_hydrated() -> void:
	hydrate_components()
	var me := multiplayer_entity
	if me:
		if me.is_multiplayer_authority():
			me._netw_table_hash = _table_hash
		else:
			if me._netw_table_hash != _table_hash:
				_table_poisoned = true
				Netw.dbg.warn("Component table hash mismatch on entity '%s': server=%d, client=%d. Table poisoned.", [entity_id, me._netw_table_hash, _table_hash])


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


## Wraps a [MultiplayerSpawner] spawn function so Networked identity is bound
## before the spawned node enters the tree.
##
## The wrapped function receives the exact gameplay payload passed to
## [method spawn_for]. The returned node is passed to
## [method NetwEntity.SpawnIdentity.bind].
## [codeblock]
## func _ready() -> void:
##     spawn_function = NetwEntity.wrap_spawn(_spawn_player)
##
## func _spawn_player(data: Dictionary) -> Node:
##     var player := PLAYER.instantiate()
##     player.spawn_index = data.spawn_index
##     return player
## [/codeblock]
##
## [method wrap_spawn] and [method spawn_for] are two halves of one contract.
## Use [method decorate_spawn] and [method spawn_identity] only for manual
## low-level spawn functions.
static func wrap_spawn(fn: Callable) -> Callable:
	return _wrapped_spawn.bind(fn)


static func _wrapped_spawn(envelope: Variant, fn: Callable) -> Node:
	var envelope_error := _spawn_envelope_error(envelope)
	if not envelope_error.is_empty():
		var msg := (
				"spawn data is not a Networked envelope: %s. Spawn through " +
				"NetwEntity.spawn_for(spawner, participant, payload). " +
				"wrap_spawn and spawn_for are two halves of one contract."
		)
		assert(false, msg % envelope_error)
		return fn.call(envelope) as Node
	var spawn_identity := SpawnIdentity.new(envelope)
	var payload: Variant = (envelope as Dictionary).get(_SPAWN_DATA_KEY)
	var node := fn.call(payload) as Node
	assert(node != null, "NetwEntity.wrap_spawn function must return a Node")
	spawn_identity.bind(node)
	return node


## Wraps [param payload] for [param participant] and calls
## [method MultiplayerSpawner.spawn].
##
## [param payload] can be any Variant supported by Godot spawn replication,
## including [Dictionary], [Array], [PackedByteArray], scalars, or
## [code]null[/code].
## Use with [method wrap_spawn] on the same [MultiplayerSpawner].
## [br][br][b]Server Only.[/b]
static func spawn_for(
		spawner: MultiplayerSpawner,
		participant: NetwParticipant,
		payload: Variant = null,
) -> Node:
	assert(
		spawner == null or spawner.multiplayer.is_server(),
		"NetwEntity.spawn_for is server-only",
	)
	if spawner == null or participant == null or participant.join == null:
		return null
	assert(
		_is_wrapped_spawn_callable(spawner.spawn_function),
		(
				"spawn_function is not wrapped. Set spawn_function = " +
				"NetwEntity.wrap_spawn(your_fn) in _ready; spawn_for " +
				"and wrap_spawn are two halves of one contract."
		),
	)
	var liveness := LivenessService.for_node(spawner)
	var route := liveness.reserve_route() if liveness else 0
	return spawner.spawn(_spawn_envelope(participant.join, payload, route))

## Decodes bindable identity from custom spawn data.
##
## Prefer [method wrap_spawn] for [MultiplayerSpawner.spawn_function].
## [method spawn_identity] remains available for low level spawn functions that
## bind identity directly.
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
		spawner: Node = null,
) -> Dictionary:
	assert(
		not data.has(_SPAWN_NETW_KEY),
		"NetwEntity.decorate_spawn: '_netw' is reserved.",
	)
	var liveness := LivenessService.for_node(spawner) if spawner else null
	var route := liveness.reserve_route() if liveness else 0
	var out := data.duplicate(true)
	out[_SPAWN_NETW_KEY] = {
		"entity_id": rj.username,
		"peer_id": rj.peer_id,
		"route": route,
	}
	return out


static func _spawn_envelope(
		rj: ResolvedJoin,
		payload: Variant = null,
		route: int = 0,
) -> Dictionary:
	return {
		_SPAWN_NETW_KEY: {
			"entity_id": rj.username,
			"peer_id": rj.peer_id,
			"route": route,
		},
		_SPAWN_DATA_KEY: payload,
	}


static func _is_spawn_envelope(value: Variant) -> bool:
	return _spawn_envelope_error(value).is_empty()


static func _spawn_envelope_error(value: Variant) -> String:
	if not value is Dictionary:
		return "expected Dictionary"
	var data := value as Dictionary
	if not data.has(_SPAWN_DATA_KEY):
		return "missing 'data'"
	return _spawn_identity_error(data)


static func _spawn_identity_error(data: Dictionary) -> String:
	if not data.has(_SPAWN_NETW_KEY):
		return "missing '_netw'"
	var netw: Variant = data.get(_SPAWN_NETW_KEY)
	if not netw is Dictionary:
		return "'_netw' must be a Dictionary"
	var identity := netw as Dictionary
	if not identity.has("entity_id"):
		return "missing '_netw.entity_id'"
	if StringName(identity.get("entity_id", "")).is_empty():
		return "'_netw.entity_id' must not be empty"
	if not identity.has("peer_id"):
		return "missing '_netw.peer_id'"
	var peer_value: Variant = identity.get("peer_id")
	if not (peer_value is int or peer_value is float):
		return "'_netw.peer_id' must be numeric"
	if int(peer_value) < 0:
		return "'_netw.peer_id' must be >= 0"
	return ""


static func _is_wrapped_spawn_callable(fn: Callable) -> bool:
	return fn.is_valid() and fn.get_method() == &"_wrapped_spawn"


## Bindable identity decoded from custom spawn data.
class SpawnIdentity extends RefCounted:
	## Decoded entity ID for the spawned node, mapped from
	## [member NetwEntity.entity_id].
	var entity_id: StringName = &""
	## Decoded peer ID for the spawned node, mapped from
	## [member NetwEntity.peer_id].
	var peer_id: int = 0
	## Decoded route ID for the spawned node, or [code]0[/code] if missing.
	var route: int = 0


	func _init(spawn_data: Dictionary) -> void:
		var envelope_error := NetwEntity._spawn_identity_error(spawn_data)
		assert(
			envelope_error.is_empty(),
			"NetwEntity.spawn_identity: %s." % envelope_error,
		)
		var netw: Dictionary = spawn_data.get(_SPAWN_NETW_KEY, { })
		entity_id = StringName(netw.get("entity_id", ""))
		peer_id = int(netw.get("peer_id", 0))
		route = int(netw.get("route", 0))


	## Binds this identity onto [param node].
	func bind(node: Node) -> Node:
		var bound := NetwEntity.bind(node, entity_id, peer_id)
		if route > 0:
			var entity := NetwEntity.of(node)
			if entity:
				# bind_route writes entity.route once the tree is
				# resolvable. The direct write covers pre-tree binds so
				# _handle_tree_entered can re-bind from the record.
				entity.route = route
				var liveness := LivenessService.for_node(node)
				if liveness:
					liveness.bind_route(route, entity)
		return bound


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

	if route > 0:
		var liveness := LivenessService.for_node(owner)
		if liveness:
			liveness.bind_route(route, self)

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

			for c in _pending_save_props:
				parent.contribute_save_property(
					c.source,
					c.virtual_name,
					c.property,
					c.save_mode,
					c.interval,
					c.mode,
					c.spawn,
					c.watch,
				)
			_pending_save_props.clear()


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
func provide(slot_id: Slot, component: Object) -> void:
	if component == null:
		_slots.erase(slot_id)
		return
	_slots[slot_id] = weakref(component)

	if slot_id == Slot.MULTIPLAYER_ENTITY:
		var ent := component as MultiplayerEntity
		if not ent.identity_hydrated.is_connected(_on_identity_hydrated):
			ent.identity_hydrated.connect(_on_identity_hydrated)
		for c in _pending_spawn_props:
			var path := property_path(c.source, c.property)
			if not path.is_empty():
				ent.add_spawn_property(path)
		_pending_spawn_props.clear()
	elif slot_id == Slot.SAVE:
		var s := component as SaveComponent
		for c in _pending_save_props:
			c.register_with(s)
		_pending_save_props.clear()

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
func require(slot_id: Slot, consumer: Callable) -> void:
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
func slot(slot_id: Slot) -> Object:
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

## The entity's [StateSynchronizer] slot, if provided.
var state: StateSynchronizer:
	get:
		return slot(Slot.STATE) as StateSynchronizer
	set(value):
		provide(Slot.STATE, value)

## The entity's [InputSynchronizer] slot, if provided.
var input: InputSynchronizer:
	get:
		return slot(Slot.INPUT) as InputSynchronizer
	set(value):
		provide(Slot.INPUT, value)

## The entity's [NetwTimeline] slot, if provided.
var timeline: NetwTimeline:
	get:
		return slot(Slot.TIMELINE) as NetwTimeline
	set(value):
		provide(Slot.TIMELINE, value)

## The entity's prediction component slot, if provided.
var prediction: PredictionComponent:
	get:
		return slot(Slot.PREDICTION) as PredictionComponent
	set(value):
		provide(Slot.PREDICTION, value)


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


## Adds a property to the entity's save component.
##
## [param save_mode] declares the persistence trust: [constant
## SaveComponent.SaveMode.SNAPSHOT] (default) has the server read the live value
## with no client channel, while [constant SaveComponent.SaveMode.CLIENT]
## replicates it client to server. [param interval] sets the per-property
## snapshot cadence in seconds ([code]0[/code] inherits
## [member delta_interval]). Calls before [SaveComponent]
## registers are buffered.
func contribute_save_property(
		source: Node,
		virtual_name: StringName,
		property: StringName,
		save_mode: SaveComponent.SaveMode = SaveComponent.SaveMode.SNAPSHOT,
		interval: float = 0.0,
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
			save_mode,
			interval,
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
		save_mode,
		interval,
		mode,
		spawn,
		watch,
	)
	_pending_save_props.append(contribution)


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


## Returns [code]true[/code] when a synchronizer other than [param exclude]
## governs the same live target as [param real_path].
##
## [param real_path] is resolved against the entity root, then compared against
## the [method SynchronizersCache.governed_targets] of every other synchronizer
## on the entity. Used by [SaveComponent] to flag a [constant
## SaveComponent.SaveMode.CLIENT] property that another synchronizer already
## drives (a double-authority mistake).
func governs_property(
		real_path: NodePath,
		exclude: MultiplayerSynchronizer = null,
) -> bool:
	if not is_instance_valid(owner) or real_path.is_empty():
		return false
	var res := owner.get_node_and_resource(real_path)
	var target_obj: Object = res[0]
	var target_sub: NodePath = res[2]
	if not target_obj or target_sub.is_empty():
		return false
	for sync in synchronizers():
		if sync == exclude:
			continue
		for t in SynchronizersCache.governed_targets(sync, owner):
			if t[0] == target_obj and t[1] == target_sub:
				return true
	return false


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
