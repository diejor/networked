class_name SpawnerComponent
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
## Contributions [b]must[/b] happen at parented-time, not at
## tree-entered: Godot reads [member replication_config] for spawn-decode
## between PackedScene instantiation and tree entry, so anything added at
## [signal NetwEntity.spawning] time is too late for the spawn packet.
##
## [br][br]
## See [method instantiate_from], [method spawn_under], and
## [method despawn] for the spawn/despawn API.
##
## Siblings react to the spawn lifecycle via [signal NetwEntity.spawning]
## (replaces the older [code]_on_entity_spawning[/code] propagate-call
## hook).
## [codeblock]
## func _notification(what: int) -> void:
##     if what == NOTIFICATION_PARENTED:
##         var entity := Netw.ctx(self).entity
##         entity.contribute_spawn_property(NodePath("..:my_property"))
##         entity.spawning.connect(_on_spawning)
##
## func _on_spawning() -> void:
##     if multiplayer.is_server():
##         hydrate_from_db()
## [/codeblock]

enum AuthorityMode {
	## Authority stays at the server peer ([code]1[/code]).
	SERVER,
	## Authority is parsed from [code]username|peer_id[/code]
	## in the owner's node name.
	CLIENT,
}

## Emitted after [member entity_id] and multiplayer authority
## are resolved, but [b]before[/b] sibling [code]_enter_tree[/code].
## Mirrors [signal NetwEntity.spawning] for callers that already hold a
## [SpawnerComponent] reference.
signal spawning

## Emitted right before [method despawn] runs, with the despawn reason.
signal despawning(reason: StringName)

## Emitted after teardown when the node leaves the tree.
signal despawned

## Which peer gets multiplayer authority over [member Node.owner].
@export var authority_mode: AuthorityMode = AuthorityMode.SERVER

## Explicit identity. When empty, [member entity_id] falls back
## to the default resolved by the authority policy.
@export var entity_id_override: StringName = &""

var _dbg: NetwHandle = Netw.dbg.handle(self)


# ── Public properties ────────────────────────────────────────────────────

## Stable identifier for the entity. Empty for templates
## (see [member is_template]).
var entity_id: StringName:
	get:
		if not entity_id_override.is_empty():
			return entity_id_override
		return _resolve_identity()


## [code]true[/code] when [member entity_id] is empty or authority
## is unresolved. Templates are editor-placed factory scenes;
## they skip the spawning lifecycle. Read-only.
var is_template: bool:
	get:
		return entity_id.is_empty() or not _has_authority_binding()


# ── Static helpers ───────────────────────────────────────────────────────

## Returns the [SpawnerComponent] under the unique name
## [code]%SpawnerComponent[/code].
## or [code]null[/code].
static func unwrap(node: Node) -> SpawnerComponent:
	var sc := node.get_node_or_null("%SpawnerComponent")
	if sc:
		return sc
	return node.get_node_or_null("%SpawnerPlayerComponent")


## Parses the multiplayer authority from a node name formatted as
## [code]username|peer_id[/code].
## Returns [param peer_id] as an [int], or [code]0[/code] if the name does
## not contain the separator.
static func parse_authority(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0


## Formats a node name in the [code]username|peer_id[/code] convention.
static func format_name(username: String, peer_id: int) -> String:
	return "%s|%d" % [username, peer_id]


## Returns an unparented copy of [param template]'s scene.
## [param configure] fires before the copy enters the tree,
## receiving the copy's [SpawnerComponent] so you can set
## [member entity_id_override] or the owner's node name.
##
## [codeblock]
## var npc := SpawnerComponent.instantiate_from(template, func(s):
##     s.entity_id_override = &"goblin_42"
## )
## parent.add_child(npc)
## [/codeblock]
static func instantiate_from(
	template: Node, configure: Callable = Callable()
) -> Node:
	var copy: Node = load(template.scene_file_path).instantiate()
	collect_from(template, copy)
	if configure.is_valid():
		var copy_spawner := unwrap(copy)
		if copy_spawner:
			configure.call(copy_spawner)
	return copy


## Copies spawn-tagged [member replication_config] properties
## from [param template] to [param copy].
## No-op when the template has no config or is out-of-tree.
static func collect_from(template: Node, copy: Node) -> void:
	var spawner := unwrap(template)
	if not spawner or not spawner.replication_config:
		return
	var cfg := spawner.replication_config
	for prop: NodePath in cfg.get_properties():
		if not cfg.property_get_spawn(prop):
			continue
		var value := SynchronizersCache.resolve_value(template, prop)
		if value != null:
			SynchronizersCache.assign_value(copy, prop, value)


# ── Lifecycle ────────────────────────────────────────────────────────────

func _init() -> void:
	name = "SpawnerComponent"
	unique_name_in_owner = true
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED:
		return
	if Engine.is_editor_hint():
		return
	
	var entity := Netw.ctx(self).entity
	if not entity:
		return
	entity.set_spawner(self)
	if not entity.owner_tree_entered.is_connected(_on_owner_tree_entered):
		entity.owner_tree_entered.connect(_on_owner_tree_entered)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	if owner:
		root_path = get_path_to(owner)
	set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_dbg.trace("_ready for %s", [owner.name if owner else "<no owner>"])

	if is_template:
		_apply_template_state()
		return


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	despawned.emit()


# Drives the entity's spawn lifecycle. Connected to
# [signal NetwEntity.owner_tree_entered] in [method _notification].
# [br][br]
# Order:
# [br]1. [method _sanitize_replication_config] - coerce all entries to
#     spawn-only / [constant SceneReplicationConfig.REPLICATION_MODE_NEVER].
#     Sibling contributions have already landed during
#     [constant Node.NOTIFICATION_PARENTED] via
#     [method NetwEntity.contribute_spawn_property].
# [br]2. [method _apply_authority] - settle authority.
# [br]3. Emit [signal NetwEntity.spawning] (and the local
#     [signal spawning] mirror) - siblings react with hydration etc.
# [br]4. [method _register_with_scene] - join the enclosing
#     [MultiplayerScene]'s visibility filters.
# [br]5. Emit [signal NetwEntity.spawned].
func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
	if not owner:
		return
	_dbg.trace("Entity '%s' entering tree.", [owner.name])
	_sanitize_replication_config()
	_apply_authority()
	if is_template:
		# Template-state setup (process disable, sync visibility) needs
		# sibling synchronizers in-tree, so it runs in _ready, not here.
		return

	var entity := Netw.ctx(self).entity
	if entity:
		entity.spawning.emit()
	spawning.emit()
	_register_with_scene()
	if entity:
		entity.spawned.emit()


# Applies [member authority_mode] to [member Node.owner].
func _apply_authority() -> void:
	match authority_mode:
		AuthorityMode.SERVER:
			owner.set_multiplayer_authority(
				MultiplayerPeer.TARGET_PEER_SERVER
			)
		AuthorityMode.CLIENT:
			var authority := parse_authority(owner.name)
			if authority != 0:
				_dbg.debug(
					"Setting authority for %s to %d",
					[owner.name, authority]
				)
				owner.set_multiplayer_authority(authority)
				set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


# [code]true[/code] when [member authority_mode] can resolve to a
# concrete peer. [code]SERVER[/code] is always bound;
# [code]CLIENT[/code] requires [code]username|peer_id[/code] in the
# owner's node name.
func _has_authority_binding() -> bool:
	match authority_mode:
		AuthorityMode.SERVER:
			return true
		AuthorityMode.CLIENT:
			return parse_authority(owner.name) != 0
	return false


## Virtual. Returns the entity id derived from subclass state.
## The base returns [code]&""[/code] (no derived identity).
func _resolve_identity() -> StringName:
	return &""


# Disables the template owner's processing and rendering.
# The server keeps the template visible only to itself;
# clients remove it.
func _apply_template_state() -> void:
	if authority_mode != AuthorityMode.CLIENT:
		return
	owner.process_mode = Node.PROCESS_MODE_DISABLED
	owner.visible = false
	#if multiplayer and not multiplayer.is_server():
		#_dbg.trace("Freeing template node `%s` on client.", [owner.name])
		#owner.queue_free()
	SynchronizersCache.sync_only_server(owner)
	pass


# ── Spawn config ─────────────────────────────────────────────────────────

## Adds [param prop] to [member replication_config] as a spawn-only entry
## (replication mode [constant SceneReplicationConfig.REPLICATION_MODE_NEVER],
## spawn flag set, sync/watch off).
##
## Intended for use from
## [signal NetwEntity.collecting_spawn_properties] handlers. Idempotent --
## adding the same path twice is a no-op.
func add_spawn_property(prop: NodePath) -> void:
	if not replication_config:
		replication_config = SceneReplicationConfig.new()
	_add_spawn_property_into(replication_config, prop)


# Adds [param prop] to [param cfg] as spawn-only.
func _add_spawn_property_into(
	cfg: SceneReplicationConfig, prop: NodePath
) -> void:
	if cfg.has_property(prop):
		_coerce_to_spawn_only(cfg, prop)
		return
	cfg.add_property(prop)
	_coerce_to_spawn_only(cfg, prop)


# Forces [param prop] to spawn-only flags.
func _coerce_to_spawn_only(
	cfg: SceneReplicationConfig, prop: NodePath
) -> void:
	cfg.property_set_replication_mode(
		prop, SceneReplicationConfig.REPLICATION_MODE_NEVER
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


# Registers the entity with the enclosing [SceneSynchronizer] so per-peer
# scene visibility filters apply.
func _register_with_scene() -> void:
	var scene := MultiplayerTree.scene_for_node(self)
	if not scene:
		_dbg.trace(
			"No enclosing MultiplayerScene for '%s'; skipping "
			+ "SceneSynchronizer track.", [owner.name]
		)
		return
	scene.synchronizer.track_node(owner)


# ── Public spawn/despawn API ─────────────────────────────────────────────

## Server-only. Spawns a copy of [member Node.owner]'s scene under
## [param parent] (defaults to owner's parent).
## [param id] sets [member entity_id_override] on the copy.
##
## [codeblock]
## var mob := spawner.spawn_under($World/Mobs, &"skeleton_1")
## var wild := spawner.spawn_under()   # same parent as template
## [/codeblock]
##
## For richer pre-tree configuration, use [method instantiate_from]
## directly so you can wire the copy before tree entry.
func spawn_under(parent: Node = null, id: StringName = &"") -> Node:
	assert(
		not multiplayer or multiplayer.is_server(),
		"spawn_under is server-only"
	)
	var copy := instantiate_from(owner, func(c: SpawnerComponent) -> void:
		if not id.is_empty():
			c.entity_id_override = id
	)
	var p := parent if parent else owner.get_parent()
	p.add_child(copy)
	return copy


## Server-only. Frees [member Node.owner] after emitting
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
			MultiplayerPeer.TARGET_PEER_SERVER
		)
	if opts.defer_free:
		owner.queue_free.call_deferred()
	else:
		owner.queue_free()
