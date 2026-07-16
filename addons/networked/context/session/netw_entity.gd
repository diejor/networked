@icon("res://addons/networked/assets/MultiplayerEntity.svg")
## Runtime identity record for one networked entity root, player or
## server-owned.
##
## One networked entity is one root node holding one [NetwEntity]. Sibling nodes
## reach the record to share identity instead of depending on each other. A
## component reads [member entity_id] and [member peer_id], learns who steers the
## entity from [member controller], reaches session services such as
## [member persistence] and [member interpolation], and follows the lifecycle
## through [signal spawning], [signal spawned], and [signal despawning].
##
## [br][br]
## Identity is sealed once, at [method arm], and never changes after. Everything
## that configures the entity runs before that moment, and everything that reads
## it runs after. [member controller] resolves at the same moment, so authority
## is settled before the node enters the tree and
## [method Node.is_multiplayer_authority] is already correct in every
## [method Node._enter_tree] and [method Node._ready], on every peer.
##
## [br][br]
## [member peer_id] classifies the entity. A non-zero value is a player and
## names the peer it represents. [code]0[/code] is a server-owned entity such as
## an NPC or world object. See [member is_player] and [enum Ownership].
##
## [br][br][b]Reaching the record[/b]
## [br]Three entry points, chosen by moment. [method of] walks up from any node
## to its record and is the everyday in-tree lookup. [method resolve]
## get-or-creates on an orphan, so it is how a root claims its own record before
## the tree. [method ensure] forces the record onto one exact node.
## [codeblock]
## NetwEntity.of(node)       # in-tree lookup, null when node is under no entity
## NetwEntity.resolve(self)  # get-or-create on an orphan, for a root's _init
## NetwEntity.ensure(root)   # force the record onto this exact root
## [/codeblock]
##
## [b]Authoring a root[/b]
## [br]A root configures itself in [code]_init[/code]. It is still an orphan
## there, so [method resolve] creates the record on the root, and the archetype
## ([member initial_controller]), the spawn-packet properties
## ([method NetwScriptModel.PropertyConfig.on_spawn]), and any lifecycle
## connections all settle in one place before the entity spawns.
## [codeblock]
## func _init() -> void:
##     var entity := NetwEntity.resolve(self)
##     entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
##     entity.spawned.connect(_on_spawned)
##     Netw.configure_property(self, &"position").on_spawn()
## [/codeblock]
##
## [b]Reaching it from a sibling[/b]
## [br]A child marks its own [method NetwScriptModel.PropertyConfig.on_spawn]
## properties in [code]_init[/code], the same as a root, because the mark records
## against the script and needs no parent, and the spawn packet collects it by
## walking the whole subtree. Connecting the entity's signals is the part that
## waits for [method Node._ready], where [method of] walks up to the resolved
## record, because a signal needs that record and a child has no parent in its
## own [code]_init[/code]. A reusable component that must also work under a
## scriptless root calls [method resolve] on
## [constant Node.NOTIFICATION_PARENTED] to provision the record itself before
## the tree.
## [codeblock]
## func _init() -> void:
##     Netw.configure_property(self, &"health").on_spawn()
##
## func _ready() -> void:
##     NetwEntity.of(self).despawning.connect(_on_despawning)
## [/codeblock]
##
## [b]Acting on an entity[/b]
## [br]The server drives the lifecycle. It creates entities through the spawn
## pipeline ([method NetwReplicationInterface.replicate]) and moves or ends one
## with [method reparent_to] and [method despawn]. A client asks the server
## through [method request_control] and reads whether it steers the entity from
## [member is_controlled_locally].
## [codeblock]
## var entity := NetwEntity.of(hit_node)
## if entity and entity.is_player:
##     eliminate(entity.peer_id)
## [/codeblock]
##
## [b]Owning identity before the tree[/b]
## [br]A spawned [Node] must own its identity before it enters the tree.
## Replicated spawns carry it in the SPAWN frame and stamp it during
## reconstruction, and [method bind] stamps it when identity rides the
## [member Node.name] channel — call it inside a
## [member MultiplayerSpawner.spawn_function] before returning the node. A
## [MultiplayerScene] with [member MultiplayerScene.gate] requires each spawned
## [Node] to own its record, which [method ensure] provides before
## [method MultiplayerScene.track_node].
class_name NetwEntity
extends RefCounted

#region Constants and enums

# Metadata key that stores the [NetwEntity] record on an entity root.
const _META_KEY := &"netw_entity"

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

## Initial control rule applied when an entity first spawns.
##
## [member peer_id] still decides whether this entity represents a player.
## [enum NetwEntity.InitialController] only decides who steers it before any
## transfer.
## [codeblock]
## NetwEntity.initial_controller -> SERVER
## # NPC, prop, or server-controlled player entity. controller == 0.
##
## NetwEntity.initial_controller -> REPRESENTED_PEER
## # Player entity controlled by its represented peer. controller == peer_id.
## [/codeblock]
enum InitialController {
	## The server controls the entity at spawn.
	SERVER,
	## The represented peer controls the entity at spawn.
	REPRESENTED_PEER,
}

## Whether players can ask the server to transfer control.
##
## [enum NetwEntity.Transfer] does not grant ownership locally. A request always
## reaches the server first and may be denied through
## [signal control_requested].
## [codeblock]
## NetwEntity.transfer -> FIXED
## # Requests are ignored. Use for fixed player entities and server props.
##
## NetwEntity.transfer -> REQUESTABLE
## # Peers may call request_control(). The server arbitrates.
## [/codeblock]
enum Transfer {
	## Control never changes through [method request_control].
	FIXED,
	## Peers may request control from the server.
	REQUESTABLE,
}

## Lifetime rule for an entity whose controller disconnects.
##
## This rule only applies when the disconnected peer controls the entity
## without being represented by it. Player representation still despawns
## through [member peer_id].
## [codeblock]
## NetwEntity.on_controller_disconnect -> REVERT_TO_SERVER
## # A dropped vehicle stays in the world.
##
## NetwEntity.on_controller_disconnect -> DESPAWN
## # A temporary controlled object disappears with its controller.
## [/codeblock]
enum DisconnectRule {
	## Control reverts to the server when the controller disconnects.
	REVERT_TO_SERVER,
	## The entity despawns when the controller disconnects.
	DESPAWN,
}

## Lifecycle position of the record, a stored fact rather than a guess from
## identity shape.
##
## Every transition funnels through one asserting edge check, so an illegal
## move is a loud debug error, never a silent reclassification. Signals are the
## edges of this machine, so they fire uniformly on every peer. See
## [member stage].
## [codeblock]
## UNBOUND     record exists, identity unbound, inert toward the wire: no
##             route, no registration, never process-disabled behind its back
## TEMPLATE    declared editor factory scene, deactivated on entry, terminal
## ARMED       identity sealed, the orphan window before tree entry
## LIVE        in the tree, routable
## DESPAWNING  teardown in flight, on every peer
## LINGERING   deactivated but rewindable, awaiting free
## FREED       owner gone, terminal
## [/codeblock]
enum Stage {
	## Identity unbound and inert. A bare programmatic node lives here.
	UNBOUND,
	## Declared editor-placed factory scene. Deactivated. Terminal.
	TEMPLATE,
	## Identity sealed, awaiting tree entry. The spawn-state orphan window.
	ARMED,
	## In the tree and routable.
	LIVE,
	## Teardown in flight on every peer. See [member active_despawn_opts].
	DESPAWNING,
	## Deactivated but rewindable, awaiting free.
	LINGERING,
	## Owner gone. Terminal.
	FREED,
}

# Legal edges of the stage machine. A reparent is not an edge: the record keeps
# its LIVE stage across the move, tracked by reparenting instead. An UNBOUND or
# ARMED record can enter DESPAWNING because a route can be bound by hand before
# activation reaches LIVE, and that entity must still tear down cleanly.
const _STAGE_EDGES := {
	Stage.UNBOUND: [Stage.TEMPLATE, Stage.ARMED, Stage.DESPAWNING],
	Stage.TEMPLATE: [],
	Stage.ARMED: [Stage.LIVE, Stage.DESPAWNING],
	Stage.LIVE: [Stage.DESPAWNING],
	Stage.DESPAWNING: [Stage.LINGERING, Stage.FREED],
	Stage.LINGERING: [Stage.FREED],
	Stage.FREED: [],
}

# Meta key marking an editor/assembly-placed spawn-point template child whose
# identity is intentionally unbound, the declared-template channel for scenes
# assembled without editor ownership on the child root.
const _SPAWN_TEMPLATE_META := &"_networked_spawn_template"

#endregion

#region Signals

## Emitted once after identity, authority, and spawn-packet properties are
## applied. The owner is in the tree, but [method Node._ready] may still be
## running.
signal spawning

## Emitted once after scene registration and the owner's [method Node._ready]
## complete.
signal spawned

## Emitted right before [method despawn] tears the entity down, carrying the
## despawn [param reason]. Listeners such as [NetwLivenessInterface] read
## [member active_despawn_opts] during this emission to branch on the despawn
## mode.
signal despawning(reason: StringName)

## Emitted once on arrival at [constant Stage.FREED], for real teardowns only.
## A [method reparent_to] leaves the tree without emitting this, since the
## record stays [constant Stage.LIVE] across the move.
signal despawned

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

## Emitted on the server when a peer requests control through
## [method request_control]. Gameplay code may inspect [param request] and
## call [method ControlRequest.deny] before the default grant path runs.
signal control_requested(peer_id: int, request: ControlRequest)

## Emitted after this entity's authority has settled.
##
## Fires on the initial spawn and on every [method reparent_to].
## Components that unregister in [code]_exit_tree[/code] reconnect their runtime
## service registration here so reparenting self-heals without
## [method Node.request_ready].
##
## A move with a [member ReparentOpts.target_global_position] is a position
## discontinuity. A camera or node that smooths or interpolates its position
## must drop that history here, or it pans across the gap. Reset whatever your
## view uses ([method Camera2D.reset_smoothing], a [Camera3D] rig's own
## interpolation, or [method Node.reset_physics_interpolation]). A
## [TPComponent] teleport delivers its destination pose separately, so reset on
## [signal TPComponent.teleport_committed] instead for that path.
## [codeblock]
## NetwEntity.of(self).reparented.connect(func(_opts): reset_smoothing())
## [/codeblock]
##
## [param reparent] is [code]null[/code] for a fresh spawn, or the in-flight
## [NetwEntity.ReparentOpts] for a reparent.
signal reparented(reparent: ReparentOpts)

## Announces that this entity's player is now the locally displayed view.
##
## Driven by the local display ([HostSceneView] on a listen-server host)
## whenever this player's scene becomes the one shown on this peer, on the
## initial display and on every return. The player lives in an offscreen
## viewport under
## [constant NetwSceneConfig.Concurrency.CONCURRENT]. When this signal has no
## connections, [HostSceneView] makes the first conventional camera current.
## Connect it to take ownership with a custom camera rig.
signal view_activated

#endregion

#region Identity

## [member Node.owner] that holds this entity.
var owner: Node

## Stable display/save/debug label for this entity.
var entity_id: StringName = &""

## Peer id of the participant this entity represents, or [code]0[/code] for a
## server-owned entity (NPC, prop, world object).
##
## A non-zero value drives [method MultiplayerScene.register_player],
## [member MultiplayerTree.local_player] tracking, and an automatic
## [method despawn] when its peer disconnects. This is the source of the
## player test. See [member is_player].
var peer_id := 0

## Compact wire route naming this entity in [NetwLivenessInterface], or
## [code]0[/code] when unroutable.
##
## Decoded from the SPAWN header.
## [NetwFrameEnvelope] frames carry this value instead of a node
## path, so a packet can always be addressed even while the node it targets is
## still spawning. See [method NetwLivenessInterface.route_of] for lookups by
## entity and [method NetwLivenessInterface.entity_of] for the reverse.
var route := 0

var _multiplayer_ref: WeakRef


# Builds the id table from the registered components, then reconciles its hash
# against the one the server authored on the spawn packet.
func _on_identity_hydrated() -> void:
	components.hydrate()
	components.reconcile()

## The [NetwMultiplayer] session this entity belongs to, or [code]null[/code]
## when it has none (an offline rig, or an orphan before activation).
##
## Mirrors [member Node.multiplayer] on the entity root once live, but handed
## over by the creator rather than re-discovered: stamped once at [method arm]
## when the spawn pipeline holds the api, or at first tree entry for a manual
## [method bind] flow. Immutable afterward, since an entity changes sessions
## only by despawn and respawn. Every session-derived member
## ([member participant], [member persistence], the state/input/broadcast
## bindings, node authority) resolves through this one handle, so "no session"
## is the single condition [code]multiplayer == null[/code].
var multiplayer: NetwMultiplayer:
	get:
		return _multiplayer_ref.get_ref() as NetwMultiplayer if _multiplayer_ref else null

## The replicated scene containing [member owner], or [code]null[/code].
var scene: MultiplayerScene:
	get:
		if not is_instance_valid(owner):
			return null
		var api := multiplayer
		if api:
			return api.scenes.scene_of(owner)
		return MultiplayerScene.of(owner)


# Stores the session handle once. A null api is ignored (a manual flow arms
# before it knows its session and stamps later at activation). Re-stamping a
# different live session is a debug error: an entity changes sessions only by
# despawn and respawn, and no cross-tree move flow exists.
func _stamp_multiplayer(api: NetwMultiplayer) -> void:
	if api == null:
		return
	var current := multiplayer
	if current == api:
		return
	assert(current == null, "multiplayer is immutable once stamped")
	_multiplayer_ref = weakref(api)


# Sets entity_id/peer_id from owner.name when a caller has not already bound
# them (the bind contract runs before add_child, so this is normally a
# no-op by the time tree entry reaches here).
func _hydrate_identity_once() -> void:
	if not is_instance_valid(owner):
		return
	if entity_id.is_empty():
		entity_id = parse_entity(owner.name)
	if peer_id == 0:
		peer_id = parse_peer(owner.name)

#endregion

#region Statics

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


## Returns an unparented copy of [param template]'s scene. [param configure]
## fires before the copy enters the tree, receiving the copy's [NetwEntity]
## so you can set [member entity_id], [member peer_id], or the owner's node
## name.
## [codeblock]
## var npc := NetwEntity.instantiate_from(template, func(e):
##     e.entity_id = &"goblin_42"
## )
## parent.add_child(npc)
## [/codeblock]
static func instantiate_from(
		template: Node,
		configure: Callable = Callable(),
) -> Node:
	var copy: Node = load(template.scene_file_path).instantiate()
	_collect_spawn_state_onto(template, copy)
	if configure.is_valid():
		configure.call(ensure(copy))
	return copy


# Copies template's .on_spawn() marked property values onto copy through the
# replication seam. A no-op when template has no session to resolve the marked
# properties through, which the manual-bind and offline flows tolerate.
static func _collect_spawn_state_onto(template: Node, copy: Node) -> void:
	var api := NetwMultiplayer.of(template)
	if not api:
		return
	for entry in api.replication.spawn_state_of(template):
		var source: Node = entry["node"]
		var prop: StringName = entry["prop"]
		var target := copy if source == template \
		else copy.get_node_or_null(template.get_path_to(source))
		if is_instance_valid(target):
			target.set(prop, source.get(prop))

#endregion

#region Archetype config

## Spawn-time control rule.
##
## An archetype config field, written while the record is [constant Stage.UNBOUND]
## and consumed once at [method arm]. The serialization-safe home is the entity
## root's own [code]_init[/code], which re-runs on every instantiate so a packed
## scene carries the rule without a marker node or metadata.
## [codeblock]
## func _init() -> void:
##     var entity := NetwEntity.resolve(self)
##     entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
## [/codeblock]
## Use [constant InitialController.REPRESENTED_PEER] when a player entity
## starts controlled by [member peer_id]. Use [constant InitialController.SERVER]
## for props, NPCs, and player entities the server should steer at spawn.
var initial_controller := InitialController.SERVER

## Player request policy for control transfer.
##
## An archetype config field written in the entity root's [code]_init[/code]
## alongside [member initial_controller], read while the record is
## [constant Stage.UNBOUND]. [constant Transfer.REQUESTABLE] lets peers call
## [method request_control]. The server emits [signal control_requested] before
## granting the request.
var transfer := Transfer.FIXED

## Controller disconnect behavior for non-player control.
##
## An archetype config field written in the entity root's [code]_init[/code]
## alongside [member initial_controller]. This does not replace the player
## representation rule. If [member peer_id] disconnects, the represented player
## entity still despawns.
var on_controller_disconnect := DisconnectRule.REVERT_TO_SERVER

#endregion

#region Control

var _controller := 0

# An explicit controller write happened before arm, so the initial_controller
# rule must not overwrite it.
var _controller_configured := false

## Peer that currently steers this entity. [code]0[/code] means the server.
##
## Resolved once at [method arm] from an explicit pre-arm write or the
## [member initial_controller] rule. Setting this property only records the
## value. Node authority follows the controller at [method arm] and through the
## server-authored transfer path ([method grant_control] / [method revoke_control]
## / a received control frame), never from a bare write, so a field write and a
## broadcast can never disagree.
## [codeblock]
## var entity := NetwEntity.of(ball)
## if entity.control_kind == NetwEntity.ControlKind.PEER:
##     show_controller(entity.controller_participant)
## [/codeblock]
var controller: int:
	get:
		return _controller if _controller_configured \
		else _initial_controller_value()
	set(value):
		_set_controller_internal(value)

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

## Participant steering [member controller], or [code]null[/code].
##
## This is independent from [member participant]. A server-owned entity can be
## controlled by a participant without representing that participant.
var controller_participant: NetwParticipant:
	get:
		return multiplayer.participant(controller) if controller != 0 and multiplayer else null

## Logical tick that produced this spawned action result.
##
## [code]-1[/code] means the entity did not come from [NetwAction]. Rides the
## SPAWN frame header directly.
var action_spawn_tick: int = -1

## Peer that requested the [NetwAction] result, or [code]0[/code]. Rides the
## SPAWN frame header directly.
var action_requester: int = 0


# The single internal controller writer: stores the value, wires the
# controller-disconnect watch, and emits the change. Bypasses the public
# setter's post-arm restriction, so arm and the transfer handlers use it.
func _set_controller_internal(value: int) -> void:
	_controller_configured = true
	var previous := _controller
	_controller = value
	if (
			value != 0
			and is_instance_valid(owner)
			and owner.multiplayer
			and is_authority
			and not owner.multiplayer.peer_disconnected.is_connected(
				_on_peer_disconnected,
			)
	):
		owner.multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	if previous != value:
		control_changed.emit(previous, value)


# Applies the settled controller to [member owner]'s multiplayer authority.
func _apply_control() -> void:
	if not is_instance_valid(owner):
		return
	var previous := owner.get_multiplayer_authority()
	var peer := _controller
	var authority_peer := peer if peer != 0 else MultiplayerPeer.TARGET_PEER_SERVER
	# Recursion onto child synchronizers is unsafe only mid-tree-entry, where
	# their network ids are not registered yet and a recurse trips a C++
	# assertion. It is safe on an orphan (nothing registered) and on a ready
	# node (registration complete), and the server always recurses. Applied on
	# the orphan at arm, authority is therefore recursive on every peer, which
	# is what makes is_multiplayer_authority() correct in every child
	# _enter_tree and _ready with no role branch inside the components.
	var recurse := is_authority or not owner.is_inside_tree() \
			or owner.is_node_ready()
	owner.set_multiplayer_authority(authority_peer, recurse)
	var previous_controller := 0 if previous == 1 else previous
	if previous_controller != peer:
		control_changed.emit(previous_controller, peer)


func _on_peer_disconnected(disconnected_peer_id: int) -> void:
	if not is_instance_valid(owner) or not owner.multiplayer or not is_authority:
		return
	if peer_id == disconnected_peer_id:
		Netw.dbg.info(
			"Peer %d disconnected. Despawning represented entity %s.",
			[disconnected_peer_id, owner.name],
		)
		var opts := DespawnOpts.new()
		opts.reason = &"peer_disconnected"
		despawn(opts)
		return

	if controller != disconnected_peer_id:
		return
	match on_controller_disconnect:
		DisconnectRule.REVERT_TO_SERVER:
			_apply_control_change(0)
		DisconnectRule.DESPAWN:
			var opts := DespawnOpts.new()
			opts.reason = &"controller_disconnected"
			despawn(opts)


# [code]true[/code] when the caller may perform a server-only action, else
# logs an error and returns [code]false[/code].
func _ensure_server_action(action: StringName) -> bool:
	if is_authority:
		return true
	var msg := "%s is server-only." % action
	Netw.dbg.error("%s", [msg], func(m): push_error(m))
	assert(false, msg)
	return false


# The controller value the initial_controller rule yields at arm when no
# explicit pre-arm write set it.
func _initial_controller_value() -> int:
	match initial_controller:
		InitialController.REPRESENTED_PEER:
			return peer_id
		_:
			return 0


## Requests control from the server.
## [br][br][b]Player request.[/b]
func request_control() -> void:
	if not is_instance_valid(owner):
		return
	var mp := owner.multiplayer
	if not mp or not mp.multiplayer_peer:
		_handle_control_request(mp.get_unique_id() if mp else 0)
		return
	if multiplayer:
		multiplayer.replication.request_control(self)


## Grants control to [param peer_id].
## [br][br][b]Server Only.[/b]
func grant_control(peer_id: int) -> void:
	if not _ensure_server_action(&"grant_control"):
		return
	_apply_control_change(peer_id)


## Revokes control and returns authority to the server.
## [br][br][b]Server Only.[/b]
func revoke_control() -> void:
	if not _ensure_server_action(&"revoke_control"):
		return
	_apply_control_change(0)


# Server-side handler for a CONTROL_REQUEST frame. Reached only on the server:
# NetwReplicationInterface only dispatches this channel to the owning route,
# and only a server ever receives a CONTROL_REQUEST in the first place.
func _handle_control_request(sender: int) -> void:
	if not is_instance_valid(owner) or not owner.multiplayer or not is_authority:
		Netw.dbg.warn(
			"Ignoring control request on non-server peer for '%s'.",
			[owner.name if is_instance_valid(owner) else "<no owner>"],
			func(m): push_warning(m),
		)
		return
	var requester := sender
	if requester == 0 and owner.multiplayer.multiplayer_peer:
		requester = owner.multiplayer.get_unique_id()
	if transfer != Transfer.REQUESTABLE:
		Netw.dbg.warn(
			"Rejecting control request from peer %d for %s. Transfer is fixed.",
			[requester, owner.name],
			func(m): push_warning(m),
		)
		return
	var request := ControlRequest.new()
	request.requester = requester
	control_requested.emit(requester, request)
	if request.denied:
		Netw.dbg.warn(
			"Control request from peer %d for %s was denied.",
			[requester, owner.name],
			func(m): push_warning(m),
		)
		return
	_apply_control_change(requester)


# Applies a control change on every peer that sees the entity. The server
# authors it and fans a CONTROL_APPLY frame to the route's interest-live
# peers; late observers learn the current controller from the spawn packet
# instead.
func _apply_control_change(peer: int) -> void:
	_set_controller_internal(peer)
	_apply_control()
	if multiplayer:
		multiplayer.replication.broadcast_control(self, peer)


# Client-side handler for a CONTROL_APPLY frame.
func _handle_control_apply(peer: int) -> void:
	_set_controller_internal(peer)
	_apply_control()

#endregion

#region Classification

## Whether this peer may author state for this entity and run its server-only
## verbs. True on the server, and true offline, since a session-less entity
## has no remote authority to defer to (offline counts as server, the
## addon-wide convention).
##
## This is the authority axis. It is distinct from
## [member is_controlled_locally] (does the local peer steer this entity) and
## from [member MultiplayerTree.is_host] (did the session open in a hosting
## role). It reads the session's [MultiplayerAPI] server predicate, which
## already answers [code]true[/code] offline and in disconnected windows, so
## unit rigs without a peer author without forging a role.
var is_authority: bool:
	get:
		return multiplayer == null or multiplayer.is_server()

## Participant represented by [member peer_id], or [code]null[/code].
##
## This resolves the live session handle for player avatars. Server-owned
## entities, props, and NPCs return [code]null[/code].
var participant: NetwParticipant:
	get:
		return multiplayer.participant(peer_id) if peer_id != 0 and multiplayer else null

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

#endregion

#region Stage and lifecycle

## The active [method reparent_to], or [code]null[/code].
##
## Set before the reparent and cleared after reparenting so
## [code]_exit_tree[/code] consumers can tell a reparent from a despawn.
var reparenting: ReparentOpts = null

## [code]true[/code] when this record is a declared editor factory scene. A
## one-line read of [member stage]. Templates are deactivated on tree entry and
## skip the spawning lifecycle.
var is_template: bool:
	get:
		return _stage == Stage.TEMPLATE

var _stage := Stage.UNBOUND

## Current lifecycle position. Read-only. Written only through the record's
## asserting internal transition, so an illegal edge is a loud debug error.
## See [enum Stage].
var stage: Stage:
	get:
		return _stage

var _ready_once_fired: bool = false

var _owner_exiting_tree: bool = false


# True while the record has not yet begun teardown and is not a terminal
# template, so a despawn verb may still open the DESPAWNING window.
func _can_begin_despawn() -> bool:
	return _stage == Stage.UNBOUND or _stage == Stage.ARMED \
			or _stage == Stage.LIVE


# The single stage mutator. Asserts the edge against the table so an illegal
# move is a loud debug error, never a silent reclassification.
func _transition(to: Stage) -> void:
	assert(
		(_STAGE_EDGES[_stage] as Array).has(to),
		"illegal stage transition %s -> %s" % [
			Stage.keys()[_stage],
			Stage.keys()[to],
		],
	)
	_stage = to


## Declares this record a [constant Stage.TEMPLATE], an editor-placed factory
## scene that stays deactivated and never spawns. Idempotent. Every peer marks
## its own copy, since the editor scene exists identically on all of them. A
## record left [constant Stage.UNBOUND] with no identity but owned by an
## enclosing editor scene self-declares here on its first tree entry.
func mark_template() -> void:
	if _stage == Stage.TEMPLATE:
		return
	_transition(Stage.TEMPLATE)
	if is_instance_valid(owner) and owner.is_inside_tree():
		_apply_template_state()


## Seals the record and applies node authority, then marks it
## [constant Stage.ARMED]. The single choke point every spawn path funnels
## through, called on the orphan before [method Node.add_child] on the pipeline
## paths so authority is recursive and correct in every child
## [method Node._enter_tree] and [method Node._ready], on every peer. The manual
## bind flows arm at their owner's first tree entry instead.
func arm(api: NetwMultiplayer = null) -> void:
	assert(_stage == Stage.UNBOUND, "arm() requires an UNBOUND record")
	_stamp_multiplayer(api)
	if not _controller_configured:
		_set_controller_internal(_initial_controller_value())
	_apply_control()
	_transition(Stage.ARMED)

## The [DespawnOpts] of the [method despawn] currently in flight, or
## [code]null[/code] outside a despawn. Set for the duration of the
## [signal despawning] emission so listeners such as
## [NetwLivenessInterface] can read the despawn mode, for example the linger
## flag that turns a route [constant NetwLivenessInterface.State.LINGERING].
var active_despawn_opts: DespawnOpts = null


# Keeps a despawned entity rewindable for opts.linger_seconds, then frees it.
# The node deactivates immediately through the same moves a template uses, so
# it stops processing, replicating, and being recorded. Its NetwTimeline
# freezes at the despawn boundary, so a late server rewind still finds where
# it was, and freeing later runs the state set's unregister so the timeline
# expires.
func _linger_then_free(opts: DespawnOpts) -> void:
	_apply_template_state()
	var tree := owner.get_tree()
	if not tree:
		owner.queue_free()
		return
	await tree.create_timer(opts.linger_seconds).timeout
	if is_instance_valid(owner):
		owner.queue_free()


# Disables the template owner's processing and rendering. The server keeps
# the template visible only to itself; clients remove it. Rendering is only
# meaningful for a visual owner, unlike a bare logic/test Node.
func _apply_template_state() -> void:
	owner.process_mode = Node.PROCESS_MODE_DISABLED
	if owner is CanvasItem:
		(owner as CanvasItem).visible = false
	elif owner is Node3D:
		(owner as Node3D).visible = false
	SynchronizersCache.sync_only_server(owner)


# Drives the live path for a spawner-produced node that was armed after it
# already entered the tree, so its first tree entry classified it inert. The
# spawn pipeline calls this right after arming such an in-tree node.
func _go_live_if_armed() -> void:
	if _stage != Stage.ARMED:
		return
	if not is_instance_valid(owner) or not owner.is_inside_tree():
		return
	_handle_tree_entered()
	if owner.is_node_ready() and not _ready_once_fired:
		_on_owner_ready()

# ---------------------------------------------------------------------------
# Control transfer rides a CONTROL_REQUEST/CONTROL_APPLY
# NetwFrameEnvelope.Channel pair dispatched straight to NetwEntity, the same
# route-addressed way SPAWN/DESPAWN/REPARENT already work. No RPC layer and
# no Node host is required for either handler.
# ---------------------------------------------------------------------------


func _attach_to(root: Node) -> void:
	owner = root
	root.set_meta(_META_KEY, self)
	if not root.tree_entered.is_connected(_handle_tree_entered):
		root.tree_entered.connect(_handle_tree_entered)
	if not root.tree_exiting.is_connected(_handle_tree_exiting):
		root.tree_exiting.connect(_handle_tree_exiting)
	if root.is_inside_tree():
		_handle_tree_entered.call_deferred()


# Drives activation on every tree entry. A first entry classifies the record
# (template, inert, or a real entity that arms and goes live); a reparent
# re-entry keeps its LIVE stage and only reapplies authority and scene
# registration for the new parent. The order matters: identity -> control
# authority -> scene registration.
func _handle_tree_entered() -> void:
	_owner_exiting_tree = false
	_parent_entity_resolved = false
	_parent_entity_ref = null

	var is_reparent := _stage == Stage.LIVE
	_hydrate_identity_once()

	# A manual bind flow, or an offline rig, arrives with no session stamped.
	# Resolve it once here, the single remaining tree walk in the record, so
	# every session-derived member below reads the stamped handle. A pipeline
	# path already stamped it at arm.
	if multiplayer == null:
		var mt := MultiplayerTree.resolve(owner)
		if mt:
			_stamp_multiplayer(mt.api)

	# A pipeline path arms the record on the orphan before this entry. A manual
	# bind flow arrives UNBOUND and arms here at first tree entry.
	if not is_reparent and _stage == Stage.UNBOUND:
		if not _classify_first_activation():
			return
		arm()

	if not is_reparent and route == 0 and multiplayer != null and is_authority:
		var liveness := NetwLivenessInterface.for_node(owner)
		if liveness:
			route = liveness.allocate_route(self)

	if route > 0:
		var liveness := NetwLivenessInterface.for_node(owner)
		if liveness:
			liveness.bind_route(route, self)

	if is_reparent:
		# A reparent runs mid-propagation here, the subtree is still
		# rebuilding, so the emit defers to post-settle when every node is
		# back in the tree. reparenting is captured now, not read inside the
		# deferred call, because reparent_to clears it right after
		# owner.reparent returns, before the deferred call runs.
		_do_emit_reparented.call_deferred(reparenting)
		# Authority followed the record through the move; reapply it for the
		# subtree's new parent. First entries already settled authority at arm.
		_apply_control()

	if not is_reparent:
		_on_identity_hydrated()

	if not owner.ready.is_connected(_on_owner_ready):
		owner.ready.connect(_on_owner_ready)
	if (peer_id != 0 or controller != 0) and owner.multiplayer \
			and not owner.multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		owner.multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	if not is_reparent:
		_transition(Stage.LIVE)
		spawning.emit()


# Classifies an UNBOUND record on its first tree entry. Returns [code]true[/code]
# when the caller should continue the go-live path, [code]false[/code] when the
# record is a template or an inert unbound node that must not go live.
func _classify_first_activation() -> bool:
	if _stage == Stage.TEMPLATE:
		_apply_template_state()
		return false
	if _stage != Stage.UNBOUND:
		return true
	if not entity_id.is_empty():
		return true
	# No bound identity. An editor-placed factory scene declares itself a
	# template. Every other unbound node stays inert: a bare programmatic node
	# forever, and a spawner-produced node until its arm stamps identity after
	# tree entry, which then activates it.
	if _is_declared_template():
		_transition(Stage.TEMPLATE)
		_apply_template_state()
	return false


# True when this record is an editor-placed factory scene. Ownership by an
# enclosing scene is the editor-placement fact; the meta is the same
# declaration for scenes assembled without stamping ownership onto the child.
func _is_declared_template() -> bool:
	if not is_instance_valid(owner):
		return false
	return owner.owner != null or owner.has_meta(_SPAWN_TEMPLATE_META)


func _do_emit_reparented(opts: ReparentOpts) -> void:
	if not is_instance_valid(owner) or not owner.is_inside_tree():
		return
	reparented.emit(opts)


func _on_owner_ready() -> void:
	if _ready_once_fired:
		return
	_ready_once_fired = true
	spawned.emit()
	reparented.emit(reparenting)


func _handle_tree_exiting() -> void:
	_owner_exiting_tree = true
	# Only a teardown that already opened the DESPAWNING window arrives at FREED.
	# A reparent leaves the tree and re-enters with the record still LIVE: the
	# server sets reparenting, and a client REPARENT frame moves the node with no
	# despawn at all. A LIVE node leaving the tree is therefore never assumed to
	# be a teardown here, so the record keeps LIVE for the re-entry to reparent.
	match _stage:
		Stage.DESPAWNING, Stage.LINGERING:
			_transition(Stage.FREED)
			despawned.emit()

#endregion

#region Verbs

## Spawns a copy of [member owner]'s scene under [param parent] (defaults to
## the owner's own parent). [param id] sets the copy's [member entity_id].
## [codeblock]
## var mob := entity.spawn_under($World/Mobs, &"skeleton_1")
## var wild := entity.spawn_under()   # same parent as template
## [/codeblock]
## For richer pre-tree configuration, use [method instantiate_from] directly
## so you can wire the copy before tree entry.
## [br][br][b]Server Only.[/b]
func spawn_under(parent: Node = null, id: StringName = &"") -> Node:
	assert(
		is_authority,
		"spawn_under is server-only",
	)
	var copy := instantiate_from(
		owner,
		func(e: NetwEntity) -> void:
			if not id.is_empty():
				bind(e.owner, id, 0)
	)
	var p := parent if parent else owner.get_parent()
	p.add_child(copy)
	return copy


## Instantiates a player copy of [member owner]'s scene from [param participant].
## [br][br][b]Server Only.[/b]
func instantiate_player(participant: NetwParticipant) -> Node:
	assert(is_authority)
	if participant == null or participant.join == null:
		return null
	var copy := instantiate_from(
		owner,
		func(e: NetwEntity) -> void:
			bind(e.owner, participant.username, participant.peer_id)
	)
	return copy


## Spawns a player copy into [param scene] from [param participant].
## [br][br][b]Server Only.[/b]
func spawn_player(participant: NetwParticipant, scene: MultiplayerScene) -> Node:
	assert(is_authority, "spawn_player is server-only")
	var copy := instantiate_player(participant)
	if copy == null:
		return null
	scene.add_player(of(copy))
	return copy


## Moves [member owner] under [param new_parent] as a networked reparent.
##
## This [NetwEntity] stays alive across the move. Components can read
## [member reparenting] in [code]_exit_tree[/code] and re-register from
## [signal reparented] after the owner enters its new parent, which is also
## where a smoothing or interpolating camera drops its history so a
## [member ReparentOpts.target_global_position] jump does not pan. When the move
## crosses [MultiplayerScene] boundaries, this method performs the scene
## admission and tracking handoff around [method Node.reparent].
## [codeblock]
## var opts := NetwEntity.ReparentOpts.new()
## opts.preserve_history = true
## entity.reparent_to(vehicle_seat, opts)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func reparent_to(new_parent: Node, opts: ReparentOpts = null) -> void:
	if not _ensure_server_action(&"reparent_to"):
		return
	assert(is_instance_valid(owner), "reparent_to requires an owner")
	assert(is_instance_valid(new_parent), "reparent_to requires a parent")
	if opts == null:
		opts = ReparentOpts.new()

	var source_scene := MultiplayerScene.of(owner)
	var destination_scene := MultiplayerScene.of(new_parent)

	reparenting = opts
	if _is_cross_scene_player_reparent(source_scene, destination_scene):
		destination_scene.prepare_player_reparent(self)

	var disconnect_after_enter := _prepare_scene_signal_handoff(
		source_scene,
		destination_scene,
	)
	if opts.target_global_position != null:
		owner.set(&"global_position", opts.target_global_position)
	owner.request_ready()
	owner.reparent(new_parent)
	if disconnect_after_enter.is_valid() \
			and owner.tree_entered.is_connected(disconnect_after_enter):
		owner.tree_entered.disconnect(disconnect_after_enter)

	if _is_cross_scene_player_reparent(source_scene, destination_scene):
		destination_scene.complete_player_reparent(self)
	elif destination_scene:
		destination_scene.track_node(owner)
	reparenting = null


func _is_cross_scene_player_reparent(
		source_scene: MultiplayerScene,
		destination_scene: MultiplayerScene,
) -> bool:
	return (
			source_scene
			and destination_scene
			and source_scene != destination_scene
			and peer_id != 0
	)


func _prepare_scene_signal_handoff(
		source_scene: MultiplayerScene,
		destination_scene: MultiplayerScene,
) -> Callable:
	if (
			not source_scene
			or not destination_scene
			or source_scene == destination_scene
	):
		return Callable()

	var source_spawned := source_scene._on_spawned
	var destination_spawned := destination_scene._on_spawned
	var source_despawned := source_scene._on_despawned
	var destination_despawned := destination_scene._on_despawned

	var flip := func(event: Signal, from: Callable, to: Callable) -> void:
		event.disconnect(from)
		var bound := to.bind(owner)
		if not event.is_connected(bound):
			event.connect(bound)

	flip.call(owner.tree_entered, source_spawned, destination_spawned)
	var flip_exit := flip.bind(
		owner.tree_exiting,
		source_despawned,
		destination_despawned,
	)
	owner.tree_entered.connect(flip_exit)
	return flip_exit


## Frees [member owner] after emitting [signal despawning] and flushing
## persisted state through [member persistence].
## [codeblock]
## # Simple teardown with default options
## entity.despawn()
##
## # Skip the save flush and defer the free
## var opts := NetwEntity.DespawnOpts.new(&"killed")
## opts.flush_save = false
## entity.despawn(opts)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func despawn(opts: DespawnOpts = null) -> void:
	if not _ensure_server_action(&"despawn"):
		return
	if not _can_begin_despawn():
		return
	if opts == null:
		opts = DespawnOpts.new()
	active_despawn_opts = opts
	_transition(Stage.DESPAWNING)
	despawning.emit(opts.reason)
	active_despawn_opts = null
	if opts.flush_save:
		var engine := persistence
		if engine:
			engine.flush()
	if owner.get_multiplayer_authority() != MultiplayerPeer.TARGET_PEER_SERVER:
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
	if opts.linger:
		_transition(Stage.LINGERING)
		_linger_then_free(opts)
		return
	if opts.defer_free:
		owner.queue_free.call_deferred()
	else:
		owner.queue_free()


# Client-side teardown entry driven by the spawn pipeline's DESPAWN frame, so
# the despawning and despawned signals fire uniformly on every peer. Liveness
# reads active_despawn_opts during the emission to drive the LINGERING route
# transition, matching the server despawn path. The pipeline still owns the
# actual free and the linger timer.
func _remote_despawn(reason: StringName, linger_seconds: float) -> void:
	if not _can_begin_despawn():
		return
	var opts := DespawnOpts.new()
	opts.reason = reason
	opts.linger = linger_seconds > 0.0
	opts.linger_seconds = linger_seconds
	active_despawn_opts = opts
	_transition(Stage.DESPAWNING)
	despawning.emit(reason)
	active_despawn_opts = null
	if opts.linger:
		_transition(Stage.LINGERING)

#endregion

#region Handles and bindings

var _interest_gate_ref: WeakRef

var _timeline_ref: WeakRef

var _interpolation: NetwInterpolationInterface.Handle

var _prediction: NetwLagCompensationInterface.PredictionHandle

var _synchronizers_cache: Array[MultiplayerSynchronizer] = []

var _synchronizers_dirty: bool = true

var _parent_entity_resolved: bool = false

var _parent_entity_ref: WeakRef

var _components: ComponentTable

## The entity's component-ID routing table, mapping each registered sub-node to
## a stable 1-byte id so an entity RPC or a masked sync frame addresses it
## without a [NodePath]. Its [member NetwEntity.ComponentTable.table_hash] rides
## the spawn packet, so a client whose structure disagrees with the server's
## [member NetwEntity.ComponentTable.wire_hash] leaves the table
## [member NetwEntity.ComponentTable.poisoned] and falls back to string paths
## and names. See [method register_component].
var components: ComponentTable:
	get:
		if _components == null:
			_components = ComponentTable.new(self)
		return _components


## Registers a sub-node as a component of the entity for RPC routing. Delegates
## to [method NetwEntity.ComponentTable.register].
func register_component(component: Node) -> void:
	components.register(component)


## Returns a [NodePath] from [param source] to [param target].
func relative_path(source: Node, target: Node) -> NodePath:
	if not is_instance_valid(source) or not is_instance_valid(target):
		return NodePath("")
	return source.get_path_to(target)


## Returns [param property] on [param source] relative to [param base].
##
## Defaults to the entity root.
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

## The entity's [NetwPersistenceInterface.PersistenceEngine], or [code]null[/code]
## when its archetype declared no [method Netw.configure_persistence].
##
## The engine reads and writes the persisted columns on the live scene, so
## [method NetwPersistenceInterface.PersistenceEngine.flush] and
## [method NetwPersistenceInterface.PersistenceEngine.hydrate] operate on the same
## saved state. Resolves through the session, so it is [code]null[/code] before the
## owner is in a [MultiplayerTree] branch.
var persistence: NetwPersistenceInterface.PersistenceEngine:
	get:
		return multiplayer.persistence.engine_for(self) if multiplayer else null


# Resolves the derived set binding of record kind on this entity, through the
# session, or null when no node of the entity marks a set of that kind or the
# owner is not yet in a tree branch. The owner's own marks win, then the route's
# derived group resolves a set a child node declares (input marks living on an
# input component under the root). The set handles resolve on demand so a
# reparent is followed for free, matching how the retained lane and the pump
# re-resolve route per pass.
func _derived_binding(record: int) -> NetwSyncSetBinding:
	if not is_instance_valid(owner):
		return null
	var api := multiplayer
	if not api:
		return null
	var binding := api.replication.derived_binding(owner, record)
	if binding:
		return binding
	var route := api.liveness.route_of(self)
	if route <= 0:
		return null
	for candidate in api.replication.derived_group(route):
		if candidate.set.record == record:
			return candidate
	return null

## The entity's derived state [NetwSyncSetBinding], the registry set handle a
## script declares with [method NetwScriptModel.PropertyConfig.state]. Resolves
## through the session, so it is [code]null[/code] before the owner is in a
## [MultiplayerTree] branch or when the owner marks no state set. This is the
## set handle a prediction engine gathers and reconciles through.
var state_binding: NetwSyncSetBinding:
	get:
		return _derived_binding(NetwSyncSet.Record.RECORD_STATE)

## The entity's derived input [NetwSyncSetBinding], the registry set handle a
## script declares with [method NetwScriptModel.PropertyConfig.input]. Resolves
## through the session, [code]null[/code] before the owner is in a
## [MultiplayerTree] branch or when the owner marks no input set. The set handle
## a windowed input stream sends and records through.
var input_binding: NetwSyncSetBinding:
	get:
		return _derived_binding(NetwSyncSet.Record.RECORD_INPUT)

## The entity's derived broadcast [NetwSyncSetBinding], the registry set handle a
## script declares with [method NetwScriptModel.PropertyConfig.broadcast]. Resolves
## through the session, [code]null[/code] before the owner is in a
## [MultiplayerTree] branch or when the owner marks no broadcast set. The set
## handle a trusted display stream fans out through, recording into no timeline.
var broadcast_binding: NetwSyncSetBinding:
	get:
		return _derived_binding(NetwSyncSet.Record.RECORD_BROADCAST)

## The ancestor visibility gate ([InterestGate]) that admits this entity's
## scene, or [code]null[/code]. Written by [MultiplayerScene], read by
## [NetwInterestInterface]. Weakref-backed, so it clears when the gate frees.
var interest_gate: InterestGate:
	get:
		return _interest_gate_ref.get_ref() as InterestGate if _interest_gate_ref \
		else null
	set(value):
		_interest_gate_ref = weakref(value) if value else null

## The entity's per-entity tick-keyed [NetwTimeline] of state and input
## snapshots, published by [NetwLagCompensationInterface], or [code]null[/code].
## Weakref-backed, so it clears when the timeline frees.
var timeline: NetwTimeline:
	get:
		return _timeline_ref.get_ref() as NetwTimeline if _timeline_ref else null
	set(value):
		_timeline_ref = weakref(value) if value else null

## Entity level prediction handle.
##
## Holds the prediction and reconciliation config a [PredictionComponent]
## declares in a scene or a caller sets in code, plus the live counters
## [method NetwLagCompensationInterface.metrics] reads. The stepping kernel
## lives in [NetwLagCompensationInterface], wired through
## [method NetwLagCompensationInterface.register_prediction]. Never
## [code]null[/code], and
## [method NetwLagCompensationInterface.PredictionHandle.is_registered]
## reports [code]false[/code] until an engine wires.
var prediction: NetwLagCompensationInterface.PredictionHandle:
	get:
		if _prediction == null:
			_prediction = NetwLagCompensationInterface.PredictionHandle.new()
			_prediction._bind(self)
		return _prediction

## Entity level interpolation handle.
##
## [NetwInterpolationInterface] reads this handle for visual root, display
## role, predicted display, and dilation settings. Per value smoothing is
## declared with [NetwInterpolate] on [NetwScriptModel.SyncConfig].
var interpolation: NetwInterpolationInterface.Handle:
	get:
		if _interpolation == null:
			_interpolation = NetwInterpolationInterface.Handle.new()
			_interpolation._bind(self)
		return _interpolation

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


## Returns [code]true[/code] when a sync stream other than [param exclude]
## governs the same live target as [param real_path].
##
## [param real_path] is resolved against the entity root, then compared against
## the [method SynchronizersCache.governed_targets] of every other synchronizer
## on the entity and against every field a derived [NetwSyncSetBinding] on the
## entity declares. Used by [NetwPersistenceInterface] to flag a persisted
## client-owned field that another stream already drives (a double-authority
## mistake).
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
	for binding in _derived_bindings():
		if binding.node() != target_obj:
			continue
		for field in binding.set.fields:
			if NodePath(":" + String(field.key)) == target_sub:
				return true
	return false


# Every derived set binding declared on this entity's route, on any of its
# nodes, resolved through the session per call the way _derived_binding is.
func _derived_bindings() -> Array[NetwSyncSetBinding]:
	var none: Array[NetwSyncSetBinding] = []
	if not is_instance_valid(owner):
		return none
	var api := multiplayer
	if not api:
		return none
	var route := api.liveness.route_of(self)
	if route <= 0:
		return none
	return api.replication.derived_group(route)


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

#endregion

#region Inner classes

## The component-ID routing table for one [NetwEntity].
##
## Maps each registered sub-node to a stable 1-byte id, so an entity RPC or a
## masked sync frame addresses a component by id instead of a [NodePath]. The
## table's [member table_hash] rides the spawn packet. When a client computes a
## different hash than the [member wire_hash] the server authored, the two
## structures disagree, so the table is [member poisoned] and every routed
## frame falls back to string paths and names rather than misrouting.
## [codeblock]
## entity.register_component(gun)
## var id := entity.components.id_for_path(NodePath("Gun"))   # 1-byte handle
## var path := entity.components.path_for_id(id)              # reverse lookup
## [/codeblock]
class ComponentTable:
	extends RefCounted

	## [code]true[/code] once a client hash mismatch was detected, so routed
	## frames fall back to string paths and names instead of ids.
	var poisoned := false

	## The 16-bit hash of this peer's own component structure, computed in
	## [method hydrate].
	var table_hash := 0

	## The 16-bit hash the server authored, carried on the spawn packet. A
	## client compares it against [member table_hash] in [method reconcile].
	var wire_hash := 0

	var _entity_ref: WeakRef
	var _registered: Array[Node] = []
	var _by_id: Dictionary[int, NodePath] = { }
	var _by_path: Dictionary[NodePath, int] = { }


	func _init(entity: NetwEntity) -> void:
		_entity_ref = weakref(entity)


	## Returns the [NodePath] registered under [param id], or an empty path when
	## [param id] is unknown.
	func path_for_id(id: int) -> NodePath:
		return _by_id.get(id, NodePath(""))


	## Returns the 1-byte id registered for [param path], or [code]0[/code] when
	## the path is absent. Since ids start at [code]1[/code], guard a lookup with
	## [method has_path] when [code]0[/code] must be told apart from a miss.
	func id_for_path(path: NodePath) -> int:
		return _by_path.get(path, 0)


	## [code]true[/code] when [param path] has a registered id.
	func has_path(path: NodePath) -> bool:
		return _by_path.has(path)


	## Registers [param component] as an addressable sub-node. A registration
	## after [method hydrate] has sealed the ids warns, since it arrives too late
	## to ride the spawn packet.
	func register(component: Node) -> void:
		var e := _entity_ref.get_ref() as NetwEntity
		if e == null or component == e.owner:
			return
		if _registered.has(component):
			return
		_registered.append(component)
		if table_hash != 0:
			Netw.dbg.warn(
				"register_component: Node '%s' registered after component table "
				+ "hydration. Configure networked properties inside _init() to "
				+ "prevent this.",
				[component.name],
				func(m): push_warning(m),
			)


	## Builds the id mapping from the registered components (sorted for
	## order-insensitivity) and computes [member table_hash].
	func hydrate() -> void:
		var e := _entity_ref.get_ref() as NetwEntity
		if e == null:
			return
		var paths: Array[NodePath] = []
		for comp in _registered:
			if is_instance_valid(comp):
				var rel := e.relative_path(e.owner, comp)
				if not rel.is_empty():
					paths.append(rel)
		paths.sort()
		_by_id.clear()
		_by_path.clear()
		for i in paths.size():
			var idx := i + 1
			if idx >= 255:
				break
			_by_id[idx] = paths[i]
			_by_path[paths[i]] = idx
		table_hash = _compute_hash(paths)


	## Reconciles the computed hash against the wire. On the server, adopts
	## [member table_hash] as [member wire_hash]. On a client, a mismatch with
	## the received [member wire_hash] leaves the table [member poisoned].
	func reconcile() -> void:
		var e := _entity_ref.get_ref() as NetwEntity
		if e == null:
			return
		if e.is_authority:
			wire_hash = table_hash
		elif wire_hash != table_hash:
			poisoned = true
			Netw.dbg.warn(
				"Component table hash mismatch on entity '%s': server=%d, "
				+ "client=%d. Table poisoned.",
				[e.entity_id, wire_hash, table_hash],
			)


	# The 16-bit hash covers the component paths (which drive comp ids) and every
	# routed script's sorted @rpc method, script property, and signal name lists
	# (which drive 1-byte method, property, and signal ids), so a version skew in
	# any of them falls back to string paths and names instead of misrouting.
	func _compute_hash(paths: Array[NodePath]) -> int:
		var e := _entity_ref.get_ref() as NetwEntity
		var s := ""
		for path in paths:
			s += str(path) + ","

		s += "|methods:"
		var scripts: Array[Script] = []
		if e and is_instance_valid(e.owner) and e.owner.get_script():
			scripts.append(e.owner.get_script())
		for comp in _registered:
			if is_instance_valid(comp) and comp.get_script():
				scripts.append(comp.get_script())
		for sc in scripts:
			var methods: Array = []
			var cfg: Dictionary = sc.get_rpc_config()
			if cfg:
				for m in cfg:
					methods.append(String(m))
			methods.sort()
			s += "|m:" + "/".join(methods)

			# Property and signal name sets drive 1-byte property/signal ids, so
			# a skew in either must poison the table exactly like a method skew.
			var props: Array = []
			var sigs: Array = []
			var base := sc
			while base != null:
				for p in base.get_script_property_list():
					if int(p.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE:
						var pn := String(p["name"])
						if not props.has(pn):
							props.append(pn)
				for sig in base.get_script_signal_list():
					var sn := String(sig["name"])
					if not sigs.has(sn):
						sigs.append(sn)
				base = base.get_base_script()
			props.sort()
			sigs.sort()
			s += "|p:" + "/".join(props)
			s += "|s:" + "/".join(sigs)

		return s.hash() & 0xFFFF


## Options bag for [method NetwEntity.despawn].
##
## Carries the knobs that control teardown behavior. Built as a
## [RefCounted] so future options (e.g., delayed-free for death
## animations) can be added without breaking call sites.
class DespawnOpts:
	extends RefCounted

	## Recorded on the despawn span and forwarded to the
	## [signal NetwEntity.despawning] signal so user code can branch
	## on the cause. Common values: [code]&"peer_disconnected"[/code],
	## [code]&"killed"[/code], [code]&"collected"[/code],
	## [code]&"timeout"[/code].
	var reason: StringName

	## When [code]true[/code] (default),
	## [method NetwPersistenceInterface.PersistenceEngine.flush] runs on the
	## despawning entity before authority revert and queue_free. A non-OK return
	## is logged at error level and the despawn proceeds - from the caller's
	## perspective despawn is infallible.
	var flush_save: bool = true

	## When [code]true[/code] (default), the [method Node.queue_free] call
	## is deferred. This guarantees the engine's next process step sees
	## the authority change before the node leaves the tree, which fixes
	## the race where a [MultiplayerSynchronizer] tries to push state
	## from a freed authority peer.
	var defer_free: bool = true

	## When [code]true[/code], the entity deactivates now but is freed only after
	## [member linger_seconds], so a late shooter can still validate against where it
	## was. Its [NetwTimeline] freezes at the despawn boundary and expires when the
	## node frees. Default [code]false[/code] keeps the cheap rule: you cannot be shot
	## after the server saw you die.
	var linger: bool = false

	## Seconds a lingering entity stays rewindable before it frees. Sized to the server
	## rewind retention window, roughly one second of ticks. Ignored unless
	## [member linger] is [code]true[/code].
	var linger_seconds: float = 1.0


	func _init(p_reason: StringName = &"") -> void:
		reason = p_reason


## Options for one [method NetwEntity.reparent_to].
class ReparentOpts:
	extends RefCounted

	## Keeps [NetwTimeline] history across the reparent.
	var preserve_history := false

	## Optional gameplay label for the reparent.
	var reason: StringName = &""

	## Destination world pose applied to [member Node.owner] before the parent
	## swap, so re-init driven by [signal NetwEntity.reparented] (interpolator
	## reset, camera smoothing) baselines on the final position instead of the
	## pre-move one. Unset ([code]null[/code]) leaves the owner where it is.
	var target_global_position: Variant = null


## Server decision object for a controller request.
##
## [NetwEntity] emits [signal NetwEntity.control_requested]
## with one [NetwEntity.ControlRequest] per request. Gameplay code may inspect
## [member requester] and call [method deny] before the default grant path
## runs.
## [codeblock]
## func _on_control_requested(peer_id: int, request: NetwEntity.ControlRequest) -> void:
##     if not can_carry(peer_id):
##         request.deny()
## [/codeblock]
class ControlRequest:
	extends RefCounted

	## Peer id reported by [method MultiplayerAPI.get_remote_sender_id].
	var requester: int = 0

	## Whether the request should be rejected.
	var denied := false


	## Rejects this request.
	func deny() -> void:
		denied = true

#endregion
