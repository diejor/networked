@tool
class_name SpawnerComponent
extends MultiplayerSynchronizer
## Wakeup contract for any networked entity (player or otherwise).
##
## [br][br]
## [b]Lifecycle (this is load-bearing):[/b]
## [br]1. [code]_init[/code] / [code]_enter_tree[/code] of this component.
## [br]2. [signal Node.tree_entered] of [member Node.owner] fires
##    [code]_on_owner_tree_entered[/code]. The connection is persisted
##    into the [code].tscn[/code] via [constant Object.CONNECT_PERSIST] in
##    [code]_validate_editor[/code]. This is the [b]only[/b] hook that runs
##    before sibling components' [code]_enter_tree[/code]; authority and
##    identity must be assigned here.
## [br]3. Inside the handler: [method _apply_authority] →
##    [member is_template] short-circuit → [method _register_with_scene] →
##    [method _dispatch_spawning] (calls [code]_on_entity_spawning(self)[/code]
##    on each sibling that defines it) → [signal spawning] (for external
##    listeners).
## [br]4. Sibling components' [code]_enter_tree[/code], then their
##    [code]_ready[/code].
## [br]5. This component's [code]_ready[/code] emits [signal spawned].
##
## [br][br]
## [b]Sibling integration:[/b] components on the same owner that need the
## entity's authority/identity to be settled implement
## [code]_on_entity_spawning(spawner: SpawnerComponent)[/code]. The dispatch
## is eager (a method call, not a signal) so siblings don't have to solve
## the connect-before-emit ordering puzzle.
##
## [codeblock]
## # Save sibling reacting to spawning:
## func _on_entity_spawning(spawner: SpawnerComponent) -> void:
##     if multiplayer.is_server():
##         hydrate_from_db()
## [/codeblock]

## How the spawned node's multiplayer authority is decided.
enum AuthorityMode {
	## Authority stays at the server peer ([code]1[/code]).
	SERVER,
	## Authority is set via [method _apply_authority], typically by
	## parsing [code]username|peer_id[/code] from the owner node name.
	## Used by [SpawnerPlayerComponent].
	CLIENT,
}

## Emitted on non-template entities after authority + identity are settled
## and scene registration is complete, but [b]before[/b] sibling components'
## [code]_enter_tree[/code]. External listeners can connect to react to a
## fresh entity coming online; siblings on the same owner should implement
## [code]_on_entity_spawning(spawner)[/code] instead — the timing guarantee
## only holds for that dispatch.
signal spawning

## Emitted on the server from [code]_ready[/code] after the entity has been
## fully configured.
signal spawned

## Emitted right before the entity is despawned via [method despawn], with
## the despawn reason.
signal despawning(reason: StringName)

## Emitted from [method _exit_tree] after teardown.
signal despawned

## How multiplayer authority is assigned to the owner node on tree entry.
## [br]- [code]SERVER[/code] (default): authority is [code]1[/code].
##   Suitable for NPCs, enemies, and most preplaced entities.
## [br]- [code]CLIENT[/code]: subclasses override [method _apply_authority].
@export var authority_mode: AuthorityMode = AuthorityMode.SERVER

## Optional explicit identity. When empty, the component falls back to
## [method _resolve_identity] (virtual, derived from subclass state).
##
## [br][br]
## [b]Read[/b] via [member entity_id] (a getter that combines override +
## derivation). [b]Write[/b] only this field — do not assign to
## [member entity_id].
@export var entity_id_override: StringName = &""

## When [code]true[/code], the editor's [b]Rebuild Spawn Properties[/b] button
## walks sibling [MultiplayerSynchronizer]s and bakes their replication-config
## properties into [member replication_config] as spawn-only state, so initial
## values reach remote clients on spawn. The build runs at edit time only —
## the runtime path no longer rebuilds.
@export var auto_track_properties: bool = true

@export_tool_button("Rebuild Spawn Properties") 
var _rebuild_btn: Callable = _rebuild_spawn_properties

var _dbg: NetwHandle = Netw.dbg.handle(self)


## The entity's stable identifier, used by [SaveComponent] as the database
## row key. Returns [member entity_id_override] when set, otherwise calls
## [method _resolve_identity]. May be empty (see [member is_template]).
var entity_id: StringName:
	get:
		if not entity_id_override.is_empty():
			return entity_id_override
		return _resolve_identity()


## [code]true[/code] when this entity has not yet been bound to a concrete
## identity or peer. Templates are editor-placed scenes acting as factories
## via [method spawn_under] / [code]instantiate_from[/code]; they short-circuit
## the spawning lifecycle. Read-only.
var is_template: bool:
	get:
		return entity_id.is_empty() or not _has_authority_binding()


## Returns the [SpawnerComponent] under the unique name
## [code]%SpawnerComponent[/code] or
## [code]%SpawnerPlayerComponent[/code] from [param node],
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


## Server-only. Returns an unparented duplicate of [param template]'s
## owner scene. [param configure] runs on the copy's [SpawnerComponent]
## immediately after instantiation — this is the only window in which
## [member entity_id_override], the owner's node name, and any subclass
## fields can be set before [method _on_owner_tree_entered] reads them.
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
	if configure.is_valid():
		var copy_spawner := unwrap(copy)
		if copy_spawner:
			configure.call(copy_spawner)
	return copy


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
	spawned.emit()


func _validate_editor() -> void:
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(
			_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST
		)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	despawned.emit()


## Runs in the unique window after [member Node.owner] enters the tree
## but before any sibling component's [code]_enter_tree[/code] fires.
## Subclasses override the policy hooks
## ([method _apply_authority], [method _resolve_identity]) to specialize
## behavior; siblings react via [code]_on_entity_spawning(spawner)[/code].
func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
	_dbg.trace("Entity '%s' entering tree.", [owner.name])
	_apply_authority()
	if is_template:
		# Template-state setup (process disable, sync visibility) needs
		# sibling synchronizers in-tree, so it runs in _ready, not here.
		return
	_register_with_scene()
	_dispatch_spawning()
	spawning.emit()


## Applies [member authority_mode] to [member Node.owner].
func _apply_authority() -> void:
	match authority_mode:
		AuthorityMode.SERVER:
			owner.set_multiplayer_authority(
				MultiplayerPeer.TARGET_PEER_SERVER
			)
		AuthorityMode.CLIENT:
			if owner.get_multiplayer_authority() != 1:
				return
			var authority := parse_authority(owner.name)
			if authority != 0:
				_dbg.debug(
					"Setting authority for %s to %d",
					[owner.name, authority]
				)
				owner.set_multiplayer_authority(authority)


## Returns [code]true[/code] when [member Node.owner] is bound to a concrete
## peer. Trivially true for [code]SERVER[/code] mode (always peer 1); for
## [code]CLIENT[/code] mode, requires [code]username|peer_id[/code] in the
## owner's node name.
func _has_authority_binding() -> bool:
	match authority_mode:
		AuthorityMode.SERVER:
			return true
		AuthorityMode.CLIENT:
			return parse_authority(owner.name) != 0
	return false


## Virtual. Subclasses override to derive [member entity_id] from their own
## state (e.g. [SpawnerPlayerComponent] returns [member username]). Returns
## [code]&""[/code] in the base, meaning "no derived identity".
func _resolve_identity() -> StringName:
	return &""


## Disables a template owner so its placeholder scene doesn't process or
## render. The server keeps the template visible only to itself; the
## client frees its copy entirely.
func _apply_template_state() -> void:
	if authority_mode != AuthorityMode.CLIENT:
		return
	owner.process_mode = Node.PROCESS_MODE_DISABLED
	owner.visible = false
	if multiplayer and not multiplayer.is_server():
		_dbg.trace("Freeing template node `%s` on client.", [owner.name])
		owner.queue_free()
	SynchronizersCache.sync_only_server(owner)


## Walks [member Node.owner]'s children and calls
## [code]_on_entity_spawning(self)[/code] on each one that defines the hook.
## This is the canonical extension point for sibling components — they get
## a settled authority + identity and a registered scene before their own
## [code]_enter_tree[/code] runs.
func _dispatch_spawning() -> void:
	for child in owner.get_children():
		if child.has_method("_on_entity_spawning"):
			child._on_entity_spawning(self)


## Adds [param prop] to [param cfg] as a spawn-only property.
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


## Builds [member replication_config] from sibling synchronizers' replication
## configs. Spawn-only: properties are tagged [code]REPLICATION_MODE_NEVER[/code]
## with spawn enabled, so the initial value transfers on spawn without ongoing
## delta replication. Subclasses extend via
## [method _populate_extra_spawn_properties].
##
## [br][br]
## [b]Editor-time only.[/b] Invoked by the [b]Rebuild Spawn Properties[/b]
## tool button or directly from tests; the runtime path no longer rebuilds.
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


## Virtual. Subclasses add component-specific spawn-only properties to
## [param cfg] (e.g. [SpawnerPlayerComponent] adds [code]username[/code]
## and the player's [code]current_scene_path[/code]).
func _populate_extra_spawn_properties(_cfg: SceneReplicationConfig) -> void:
	pass


## Tool-button entry point: rebuilds [member replication_config] from the
## current scene state. Safe to call repeatedly.
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


## Registers the entity with the enclosing [SceneSynchronizer] so per-peer
## scene visibility filters apply.
func _register_with_scene() -> void:
	var scene := MultiplayerTree.scene_for_node(self)
	if not scene:
		_dbg.trace(
			"No enclosing MultiplayerScene for '%s'; skipping "
			+ "SceneSynchronizer track.", [owner.name]
		)
		return
	scene.synchronizer.track_node(owner)


## Server-only. Convenience for the non-player case: clones [member Node.owner]
## as a sibling of [param parent] (or [member Node.owner]'s own parent when
## [param parent] is [code]null[/code]) and adds it to the tree.
##
## [param id] is assigned to the copy's [member entity_id_override] before
## tree entry so [method _on_owner_tree_entered] sees a concrete identity.
##
## For player flows or anything that needs richer pre-tree-entry configuration,
## use [method instantiate_from] directly and place the copy yourself.
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


## Server-only. Tears down [member Node.owner] in the canonical
## order: emit [signal despawning], flush [SaveComponent], revert
## authority to the server, then [method Node.queue_free].
##
## Despawn is infallible from the caller's perspective. A
## [SaveComponent.flush] failure is logged at error level and the
## despawn proceeds. Callers needing transactional semantics should
## flush themselves first and pass [code]flush_save: false[/code]
## in [param opts].
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
