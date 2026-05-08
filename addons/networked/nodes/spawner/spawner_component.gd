@tool
class_name SpawnerComponent
extends MultiplayerSynchronizer
## Orchestration point for a networked entity.
##
## [member replication_config] bundles properties
## into the spawn packet so initial state arrives with the entity.
## 
## [br][br]
## See [method instantiate_from], [method spawn_under], and
## [method despawn] for the spawn/despawn API.
##
## Components on the same owner implement
## [code]_on_entity_spawning(spawner: SpawnerComponent)[/code]
## instead of relying on [code]_enter_tree[/code] ordering.
## [codeblock]
## func _on_entity_spawning(spawner: SpawnerComponent) -> void:
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
## Siblings needing settled identity should implement
## [code]_on_entity_spawning(spawner)[/code] instead -- the timing
## guarantee only holds for that dispatch.
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

## When [code]true[/code], the editor's [b]Rebuild Spawn Properties[/b]
## button populates [member replication_config] from sibling
## [MultiplayerSynchronizer]s so their properties transfer on spawn.
@export var auto_track_properties: bool = true

@export_tool_button("Rebuild Spawn Properties") 
var _rebuild_btn: Callable = _rebuild_spawn_properties

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


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	if owner:
		root_path = get_path_to(owner)
	set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _ready() -> void:
	if Engine.is_editor_hint():
		_validate_editor()
		return
	_dbg.trace("_ready for %s", [owner.name if owner else "<no owner>"])

	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		("SpawnerComponent: pre-init hook missing on '%s'. The signal "
		+ "'tree_entered' on the owner is normally wired to "
		+ "_on_owner_tree_entered via CONNECT_PERSIST in _validate_editor. "
		+ "Open '%s' in the editor and re-save it to repair the connection.")
		% [owner.name, owner.scene_file_path]
	)
	if is_template:
		_apply_template_state()
		return


func _validate_editor() -> void:
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(
			_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST
		)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	despawned.emit()


# Runs in the unique window after [member Node.owner] enters the tree
# but before any sibling component's [code]_enter_tree[/code] fires.
# Subclasses override the policy hooks
# ([method _apply_authority], [method _resolve_identity]) to specialize
# behavior; siblings react via [code]_on_entity_spawning(spawner)[/code].
func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
	_dbg.trace("Entity '%s' entering tree.", [owner.name])
	_apply_authority()
	if is_template:
		# Template-state setup (process disable, sync visibility) needs
		# sibling synchronizers in-tree, so it runs in _ready, not here.
		return
	_dispatch_spawning()
	_register_with_scene()
	
	spawning.emit()


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
	if multiplayer and not multiplayer.is_server():
		_dbg.trace("Freeing template node `%s` on client.", [owner.name])
		owner.queue_free()
	SynchronizersCache.sync_only_server(owner)


func _dispatch_spawning() -> void:
	owner.propagate_call("_on_entity_spawning", [self])


# ── Spawn config ─────────────────────────────────────────────────────────

# Adds [param prop] to [param cfg] as spawn-only.
func _add_spawn_property_into(
	cfg: SceneReplicationConfig, prop: NodePath
) -> void:
	if cfg.has_property(prop):
		return
	cfg.add_property(prop)
	cfg.property_set_replication_mode(
		prop, SceneReplicationConfig.REPLICATION_MODE_NEVER
	)
	cfg.property_set_spawn(prop, true)
	cfg.property_set_sync(prop, false)
	cfg.property_set_watch(prop, false)


# Rebuilds [member replication_config] from sibling synchronizers.
func _build_spawn_config() -> void:
	if not auto_track_properties:
		return
	var cfg := SceneReplicationConfig.new()
	var syncs := SynchronizersCache.get_client_synchronizers(owner)
	for sync in syncs:
		if sync == self or not sync.replication_config:
			continue
		for prop: NodePath in sync.replication_config.get_properties():
			_add_spawn_property_into(cfg, prop)
	_populate_extra_spawn_properties(cfg)
	replication_config = cfg


## Virtual. Adds subclass properties to [param cfg] as
## spawn-only state.
func _populate_extra_spawn_properties(_cfg: SceneReplicationConfig) -> void:
	pass


# Tool-button entry point: rebuilds [member replication_config] from the
# current scene state. Safe to call repeatedly.
func _rebuild_spawn_properties() -> void:
	if not Engine.is_editor_hint() or not owner:
		return
	_build_spawn_config()
	notify_property_list_changed()
	update_configuration_warnings()


func _get_configuration_warnings() -> PackedStringArray:
	var w := PackedStringArray()
	if (
		auto_track_properties
		and (
			not replication_config
			or replication_config.get_properties().is_empty()
		)
	):
		w.append(
			"auto_track_properties is on but replication_config is empty. "
			+ "Press 'Rebuild Spawn Properties' to bake sibling-synchronizer "
			+ "properties into spawn-only state."
		)
	return w


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
