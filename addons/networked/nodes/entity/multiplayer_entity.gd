class_name MultiplayerEntity
extends MultiplayerSynchronizer
## Orchestration point for a networked entity.
##
## [member replication_config] bundles properties into the spawn packet
## so initial state arrives with the entity. Sibling components contribute
## paths through [method NetwEntity.contribute_spawn_property] from their
## own [constant Node.NOTIFICATION_PARENTED]; the inspector's Replication
## panel can also pre-populate the list (its flags are coerced to
## spawn-only at runtime).
##
## [br][br]
## The synchronizer itself always has multiplayer authority [code]1[/code]
## so the server can always issue spawn and despawn commands. The
## [member owner]'s authority is derived from [member controller].
##
## [br][br]
## Contributions [b]must[/b] happen at parented-time, not at
## tree-entered: Godot reads [member replication_config] for spawn-decode
## between PackedScene instantiation and tree entry, so anything added at
## [signal NetwEntity.spawning] time is too late for the spawn packet.
##
## [br][br]
## See [method instantiate_from], [method spawn_under],
## [method spawn_player], and [method despawn] for the spawn/despawn interface.
##
## Siblings react to the spawn lifecycle via [signal NetwEntity.spawning]
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         var entity := Netw.ctx(self).entity
##         entity.contribute_spawn_property(self, &"my_property")
##         entity.spawning.connect(_on_spawning)
##
## func _on_spawning() -> void:
##     if multiplayer.is_server():
##         hydrate_from_db()
## [/codeblock]

## Initial control rule applied when an entity first spawns.
##
## [member peer_id] still decides whether this entity represents a player.
## [enum InitialController] only decides who steers it before any transfer.
## [codeblock]
## MultiplayerEntity.initial_controller -> SERVER
## # NPC, prop, or server-controlled player entity. controller == 0.
##
## MultiplayerEntity.initial_controller -> REPRESENTED_PEER
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
## [enum Transfer] does not grant ownership locally. A request always reaches
## the server first and may be denied through [signal control_requested].
## [codeblock]
## MultiplayerEntity.transfer -> FIXED
## # Requests are ignored. Use for fixed player entities and server props.
##
## MultiplayerEntity.transfer -> REQUESTABLE
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
## MultiplayerEntity.on_controller_disconnect -> REVERT_TO_SERVER
## # A dropped vehicle stays in the world.
##
## MultiplayerEntity.on_controller_disconnect -> DESPAWN
## # A temporary controlled object disappears with its controller.
## [/codeblock]
enum DisconnectRule {
	## Control reverts to the server when the controller disconnects.
	REVERT_TO_SERVER,
	## The entity despawns when the controller disconnects.
	DESPAWN,
}

## Emitted after [member entity_id] and multiplayer authority
## are resolved, but [b]before[/b] sibling [method Node._enter_tree].
## Mirrors [signal NetwEntity.spawning] for callers that already hold a
## [MultiplayerEntity] reference.
signal spawning

## Emitted right before [method despawn] runs, with the despawn reason.
signal despawning(reason: StringName)

## Emitted after teardown when the node leaves the tree.
signal despawned

## Emitted after [member controller] changes.
signal control_changed(previous_peer: int, peer: int)

## Emitted on the server when a peer requests control.
signal control_requested(peer_id: int, request: ControlRequest)

## Spawn-time control rule.
##
## Use [constant InitialController.REPRESENTED_PEER] when a player entity
## starts controlled by [member peer_id]. Use [constant InitialController.SERVER]
## for props, NPCs, and player entities the server should steer at spawn.
## [codeblock]
## MultiplayerEntity.initial_controller -> REPRESENTED_PEER
## # Player entity controlled by its represented peer.
##
## MultiplayerEntity.initial_controller -> SERVER
## # Server-controlled entity.
## [/codeblock]
@export var initial_controller := InitialController.SERVER

## Player request policy for control transfer.
##
## [constant Transfer.REQUESTABLE] lets peers call [method request_control].
## The server emits [signal control_requested] before granting the request.
## [codeblock]
## MultiplayerEntity.transfer -> REQUESTABLE
## MultiplayerEntity.control_requested -> _can_control
##
## func _can_control(peer_id: int, request: ControlRequest) -> void:
##     if not is_close_enough(peer_id):
##         request.deny()
## [/codeblock]
@export var transfer := Transfer.FIXED

## Controller disconnect behavior for non-player control.
##
## This does not replace the player representation rule. If [member peer_id]
## disconnects, the represented player entity still despawns.
## [codeblock]
## MultiplayerEntity.on_controller_disconnect -> REVERT_TO_SERVER
## # Vehicles stay in the world when their driver leaves.
##
## MultiplayerEntity.on_controller_disconnect -> DESPAWN
## # Temporary controlled objects disappear with their controller.
## [/codeblock]
@export var on_controller_disconnect := DisconnectRule.REVERT_TO_SERVER

var _pending_entity_id: StringName = &""
var _pending_peer_id := 0
var _pending_controller := 0
var _pending_controller_binding_set := false

## Stable entity label mirrored to [member NetwEntity.entity_id].
## If empty, the spawn lifecycle derives it from [member Node.name].
@export var entity_id: StringName = &"":
	get:
		var entity := _get_entity_record()
		if entity:
			return entity.entity_id
		return _pending_entity_id
	set(value):
		var entity := _get_entity_record()
		if entity:
			entity.entity_id = value
		else:
			_pending_entity_id = value

## Peer this entity represents, propagated to
## [member NetwEntity.peer_id]. Drives auto-despawn on
## disconnect, [member MultiplayerTree.local_player] tracking, and
## scene registration. [code]0[/code] for non-player entities.
var peer_id := 0:
	get:
		var entity := _get_entity_record()
		if entity:
			return entity.peer_id
		return _pending_peer_id
	set(value):
		var entity := _get_entity_record()
		if entity:
			entity.peer_id = value
		else:
			_pending_peer_id = value

## Peer currently steering [member Node.owner]. [code]0[/code] is server.
var controller := 0:
	get:
		return _effective_controller()
	set(value):
		_set_controller_value(value, true)

## Whether [member controller] overrides [member initial_controller].
##
## This exists for spawn transport. [member controller] value [code]0[/code]
## can mean either "derive from [member initial_controller]" or "the server
## explicitly controls this entity now". Late joiners need this flag to tell
## those states apart.
## [codeblock]
## MultiplayerEntity.controller_binding_set -> false
## # Derive controller from initial_controller.
##
## MultiplayerEntity.controller_binding_set -> true
## # Use the stored controller value.
## [/codeblock]
var controller_binding_set := false:
	get:
		return _pending_controller_binding_set
	set(value):
		_pending_controller_binding_set = value


func _get_entity_record() -> NetwEntity:
	if not is_instance_valid(owner):
		return null
	if owner.has_meta(NetwEntity._META_KEY):
		return owner.get_meta(NetwEntity._META_KEY) as NetwEntity
	return null


var _dbg: NetwHandle = Netw.dbg.handle(self)

## [code]true[/code] when [member entity_id] is empty or authority
## is unresolved. Templates are editor-placed factory scenes;
## they skip the spawning lifecycle. Read-only.
var is_template: bool:
	get:
		return entity_id.is_empty() or not _has_authority_binding()


## Returns the [MultiplayerEntity] under the unique name
## [code]%MultiplayerEntity[/code], or [code]null[/code].
static func unwrap(node: Node) -> MultiplayerEntity:
	return node.get_node_or_null("%MultiplayerEntity")


## Returns an unparented copy of [param template]'s scene.
## [param configure] fires before the copy enters the tree,
## receiving the copy's [MultiplayerEntity] so you can set
## [member entity_id], [member peer_id], or the owner's node name.
##
## [codeblock]
## var npc := MultiplayerEntity.instantiate_from(template, func(s):
##     s.entity_id = &"goblin_42"
## )
## parent.add_child(npc)
## [/codeblock]
static func instantiate_from(
		template: Node,
		configure: Callable = Callable(),
) -> Node:
	var copy: Node = load(template.scene_file_path).instantiate()
	collect_from(template, copy)
	if configure.is_valid():
		var copy_entity := unwrap(copy)
		if copy_entity:
			configure.call(copy_entity)
	return copy


## Copies spawn-tagged [member replication_config] properties
## from [param template] to [param copy].
## No-op when the template has no config or is out-of-tree.
static func collect_from(template: Node, copy: Node) -> void:
	var entity := unwrap(template)
	if not entity or not entity.replication_config:
		return
	var cfg := entity.replication_config
	for prop: NodePath in cfg.get_properties():
		if not cfg.property_get_spawn(prop):
			continue
		var value := SynchronizersCache.resolve_value(template, prop)
		if value != null:
			SynchronizersCache.assign_value(copy, prop, value)

# Lifecycle.


func _init() -> void:
	name = "MultiplayerEntity"
	unique_name_in_owner = true


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED:
		return
	if Engine.is_editor_hint():
		return

	var entity := Netw.ctx(self).entity
	if not entity or not entity.owner:
		return
	entity.multiplayer_entity = self
	_ensure_replication_config()
	entity.contribute_spawn_property(self, &"controller")
	entity.contribute_spawn_property(self, &"controller_binding_set")
	_hydrate_identity_once(entity)
	_hydrate_controller_once(entity)
	if not entity.owner_tree_entered.is_connected(_on_owner_tree_entered):
		entity.owner_tree_entered.connect(_on_owner_tree_entered)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	if owner:
		root_path = get_path_to(owner)
	set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
	if controller_binding_set:
		_apply_control()


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_dbg.trace("_ready for %s", [owner.name if owner else "<no owner>"])

	if is_template:
		_apply_template_state()
		return
	if (
			(peer_id != 0 or controller != 0)
			and not multiplayer.peer_disconnected.is_connected(
				_on_peer_disconnected,
			)
	):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if _is_local_represented_peer():
		var mt := MultiplayerTree.resolve(self)
		if mt and mt.local_player == owner:
			mt.local_player = null
	despawned.emit()


# Starts the entity lifecycle after identity is available.
#
# Spawn contributions are locked before sibling components hydrate. Control
# authority is applied before [signal NetwEntity.spawning]. Scene registration
# still follows representation through [member peer_id].
# [codeblock]
# spawn properties -> control authority -> spawning -> scene registration
# [/codeblock]
func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
	if not owner:
		return
	_dbg.trace("Entity '%s' entering tree.", [owner.name])
	var entity := Netw.ctx(self).entity
	if entity:
		_hydrate_identity_once(entity)
		_hydrate_controller_once(entity)
	_sanitize_replication_config()
	_apply_control()
	if is_template:
		# Template-state setup (process disable, sync visibility) needs
		# sibling synchronizers in-tree, so it runs in _ready, not here.
		return

	if entity:
		entity.spawning.emit()
	spawning.emit()
	_register_with_scene()
	if entity:
		entity.spawned.emit()


# Applies the effective controller to [member Node.owner].
func _apply_control() -> void:
	if not owner:
		return
	var previous := owner.get_multiplayer_authority()
	var peer := _effective_controller()
	var authority_peer := peer if peer != 0 else MultiplayerPeer.TARGET_PEER_SERVER
	_dbg.debug(
		"Setting authority for %s to %d",
		[owner.name, authority_peer],
	)
	var is_server := not multiplayer or multiplayer.is_server()
	if is_server:
		# Server establishes authority recursively across the tree, then
		# re-pins the MultiplayerEntity itself to the server (1).
		owner.set_multiplayer_authority(authority_peer, true)
		set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
	else:
		# Clients must not call set_multiplayer_authority recursively on
		# child synchronizers during tree-entry/spawning because Godot's
		# replication system has not yet registered their network IDs (which
		# would trigger a C++ assertion error).
		#
		# Instead, the client relies on Godot's MultiplayerSpawner to
		# automatically synchronize the child synchronizers' authorities from
		# the server:
		#
		#  Server (recursive+re-pin)        Client (spawner-synced spawn)
		#  |- owner (auth = P)              |- owner (auth = P) [Set here]
		#  |- PlayerSync (auth = P)         |- PlayerSync (auth = P) [Spawner]
		#  +- MultiplayerEntity (auth = 1)  +- MultiplayerEntity (auth = 1) [Spawner]
		#
		if is_node_ready():
			# Dynamic mid-game control change on client. Safe to recurse.
			owner.set_multiplayer_authority(authority_peer, true)
			set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		else:
			# Initial spawn phase on client. Do not touch synchronizers.
			owner.set_multiplayer_authority(authority_peer, false)

	var previous_controller := 0 if previous == 1 else previous
	if previous_controller != peer:
		control_changed.emit(previous_controller, peer)
		var entity := _get_entity_record()
		if entity:
			entity.control_changed.emit(previous_controller, peer)


func _effective_controller() -> int:
	if controller_binding_set:
		return _pending_controller
	match initial_controller:
		InitialController.SERVER:
			return 0
		InitialController.REPRESENTED_PEER:
			return peer_id
	return 0


func _hydrate_identity_once(entity: NetwEntity) -> void:
	if entity.entity_id.is_empty():
		if not _pending_entity_id.is_empty():
			entity.entity_id = _pending_entity_id
		elif owner:
			entity.entity_id = NetwEntity.parse_entity(owner.name)
	_pending_entity_id = entity.entity_id
	if entity.peer_id == 0:
		if _pending_peer_id != 0:
			entity.peer_id = _pending_peer_id
		elif owner:
			entity.peer_id = NetwEntity.parse_peer(owner.name)
	_pending_peer_id = entity.peer_id


func _hydrate_controller_once(entity: NetwEntity) -> void:
	if _pending_controller_binding_set:
		entity._pending_controller = _pending_controller
	else:
		_pending_controller = entity._pending_controller
	controller_binding_set = _pending_controller_binding_set


# Keeps the synchronizer valid even when identity uses owner.name transport.
func _ensure_replication_config() -> void:
	if not replication_config:
		replication_config = SceneReplicationConfig.new()


# [code]true[/code] when [member initial_controller] can resolve.
func _has_authority_binding() -> bool:
	match initial_controller:
		InitialController.SERVER:
			return true
		InitialController.REPRESENTED_PEER:
			return peer_id != 0 or NetwEntity.parse_peer(owner.name) != 0
	return false


# Disables the template owner's processing and rendering.
# The server keeps the template visible only to itself;
# clients remove it.
func _apply_template_state() -> void:
	owner.process_mode = Node.PROCESS_MODE_DISABLED
	owner.visible = false
	#if multiplayer and not multiplayer.is_server():
	#_dbg.trace("Freeing template node `%s` on client.", [owner.name])
	#owner.queue_free()
	SynchronizersCache.sync_only_server(owner)
	pass

# Spawn config.


## Adds [param prop] to [member replication_config] as a spawn-only entry
## (replication mode [constant SceneReplicationConfig.REPLICATION_MODE_NEVER],
## spawn flag set, sync/watch off).
##
## Intended for use during spawn-property contributions. Idempotent --
## adding the same path twice is a no-op.
func add_spawn_property(prop: NodePath) -> void:
	if not replication_config:
		replication_config = SceneReplicationConfig.new()
	_add_spawn_property_into(replication_config, prop)


# Adds [param prop] to [param cfg] as spawn-only.
func _add_spawn_property_into(
		cfg: SceneReplicationConfig,
		prop: NodePath,
) -> void:
	if cfg.has_property(prop):
		_coerce_to_spawn_only(cfg, prop)
		return
	cfg.add_property(prop)
	_coerce_to_spawn_only(cfg, prop)


# Forces [param prop] to spawn-only flags.
func _coerce_to_spawn_only(
		cfg: SceneReplicationConfig,
		prop: NodePath,
) -> void:
	cfg.property_set_replication_mode(
		prop,
		SceneReplicationConfig.REPLICATION_MODE_NEVER,
	)
	cfg.property_set_spawn(prop, true)
	cfg.property_set_sync(prop, false)
	cfg.property_set_watch(prop, false)


# Coerces every property in [member replication_config] to spawn-only,
# regardless of how it was originally configured (inspector or sibling).
func _sanitize_replication_config() -> void:
	if not replication_config:
		return
	for prop: NodePath in replication_config.get_properties():
		_coerce_to_spawn_only(replication_config, prop)


# Registers the entity with the enclosing [MultiplayerScene] so per-peer
# scene visibility filters apply. Scene-owned enrollment - the scene's
# layer/gate is the authoritative admission state; [InterestComponent]
# only handles additional generic layers.
func _register_with_scene() -> void:
	var scene := MultiplayerTree.scene_for_node(self)
	if not scene:
		_dbg.debug(
			"No enclosing MultiplayerScene for '%s'; skipping "
			+ "scene track.",
			[owner.name],
		)
		return
	if peer_id != 0:
		scene.register_player(owner)
		_assign_local_player_if_needed()
	else:
		scene.track_node(owner)


func _assign_local_player_if_needed() -> void:
	if not _is_local_represented_peer():
		return
	var mt := MultiplayerTree.resolve(self)
	if mt:
		mt.local_player = owner


func _is_local_represented_peer() -> bool:
	if peer_id == 0:
		return false
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return false
	return peer_id == multiplayer.get_unique_id()


func _on_peer_disconnected(disconnected_peer_id: int) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	if peer_id == disconnected_peer_id:
		_dbg.info(
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


## Requests control from the server.
## [br][br][b]Player request.[/b]
func request_control() -> void:
	_rpc_request_control.rpc_id(MultiplayerPeer.TARGET_PEER_SERVER)


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


## Sets [member NetwEntity.controller] from the entity record.
func set_controller(peer: int) -> void:
	_set_controller_value(peer, true)
	_apply_control()


@rpc("any_peer", "call_local", "reliable")
func _rpc_request_control() -> void:
	if not multiplayer or not multiplayer.is_server():
		_dbg.warn(
			"Ignoring control request on non-server peer %d.",
			[multiplayer.get_unique_id() if multiplayer else 0],
			func(m): push_warning(m),
		)
		return
	var requester := multiplayer.get_remote_sender_id()
	if requester == 0 and multiplayer.multiplayer_peer:
		requester = multiplayer.get_unique_id()
	if transfer != Transfer.REQUESTABLE:
		_dbg.warn(
			"Rejecting control request from peer %d for %s. Transfer is fixed.",
			[requester, owner.name if owner else "<no owner>"],
			func(m): push_warning(m),
		)
		return
	var request := ControlRequest.new()
	request.requester = requester
	control_requested.emit(requester, request)
	if request.denied:
		_dbg.warn(
			"Control request from peer %d for %s was denied.",
			[requester, owner.name if owner else "<no owner>"],
			func(m): push_warning(m),
		)
		return
	_apply_control_change(requester)


@rpc("authority", "call_local", "reliable")
func _rpc_apply_control(peer: int) -> void:
	_set_controller_value(peer, true)
	_apply_control()


func _apply_control_change(peer: int) -> void:
	if multiplayer and multiplayer.multiplayer_peer:
		_rpc_apply_control.rpc(peer)
	else:
		_rpc_apply_control(peer)


func _set_controller_value(peer: int, bound: bool) -> void:
	_pending_controller = peer
	_pending_controller_binding_set = bound
	var entity := _get_entity_record()
	if entity:
		entity._pending_controller = peer
	if (
			peer != 0
			and multiplayer
			and multiplayer.is_server()
			and not multiplayer.peer_disconnected.is_connected(
				_on_peer_disconnected,
			)
	):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)


func _ensure_server_action(action: StringName) -> bool:
	if not multiplayer or multiplayer.is_server():
		return true
	var msg := "%s is server-only." % action
	_dbg.error("%s", [msg], func(m): push_error(m))
	assert(false, msg)
	return false

# Public spawn/despawn API.


## Spawns a copy of [member Node.owner]'s scene under
## [param parent] (defaults to owner's parent).
## [param id] sets [member entity_id] on the copy.
##
## [codeblock]
## var mob := spawner.spawn_under($World/Mobs, &"skeleton_1")
## var wild := spawner.spawn_under()   # same parent as template
## [/codeblock]
##
## For richer pre-tree configuration, use [method instantiate_from]
## directly so you can wire the copy before tree entry.
## [br][br][b]Server Only.[/b]
func spawn_under(parent: Node = null, id: StringName = &"") -> Node:
	assert(
		not multiplayer or multiplayer.is_server(),
		"spawn_under is server-only",
	)
	var copy := instantiate_from(
		owner,
		func(c: MultiplayerEntity) -> void:
			if not id.is_empty():
				NetwEntity.bind(c.owner, id, 0)
	)
	var p := parent if parent else owner.get_parent()
	p.add_child(copy)
	return copy


## Instantiates a player copy from [param rj].
## [br][br][b]Server Only.[/b]
func instantiate_player(rj: ResolvedJoin) -> Node:
	assert(multiplayer.is_server())
	var copy := instantiate_from(
		owner,
		func(c: MultiplayerEntity) -> void:
			NetwEntity.bind(c.owner, rj.username, rj.peer_id)
	)
	return copy


## Spawns a player copy into [param scene] from [param rj].
## [br][br][b]Server Only.[/b]
func spawn_player(rj: ResolvedJoin, scene: MultiplayerScene) -> Node:
	assert(multiplayer.is_server(), "spawn_player is server-only")
	var copy := instantiate_player(rj)
	scene.add_player(copy)
	return copy


## Frees [member Node.owner] after emitting
## [signal despawning] and flushing the [SaveComponent].
##
## [codeblock]
## # Simple teardown with default options
## spawner.despawn()
##
## # Skip the save flush and defer the free
## var opts := DespawnOpts.new(&"killed")
## opts.flush_save = false
## spawner.despawn(opts)
## [/codeblock]
## [br][br][b]Server Only.[/b]
func despawn(opts: DespawnOpts = null) -> void:
	assert(multiplayer.is_server(), "despawn is server-only")
	if opts == null:
		opts = DespawnOpts.new()
	despawning.emit(opts.reason)
	if opts.flush_save:
		var save: SaveComponent = owner.get_node_or_null("%SaveComponent")
		if save:
			save.flush()
	if (
			owner.get_multiplayer_authority()
			!= MultiplayerPeer.TARGET_PEER_SERVER
	):
		owner.set_multiplayer_authority(
			MultiplayerPeer.TARGET_PEER_SERVER,
		)
	if opts.defer_free:
		owner.queue_free.call_deferred()
	else:
		owner.queue_free()
