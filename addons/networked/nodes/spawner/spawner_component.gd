@tool
class_name SpawnerComponent
extends NetwComponent
## Wakeup contract for any networked entity that is not a player.

## How the spawned node's multiplayer authority is decided.
enum AuthorityPolicy {
	## Authority stays at the server peer ([code]1[/code]).
	SERVER,
	## Authority is set to [member authority_peer]. Used by the player
	## flow via [SpawnerPlayerComponent.AuthorityMode.CLIENT].
	FIXED_PEER,
	## Authority is left untouched. The caller (or an
	## [SpawnerComponent] subclass) is expected to set it elsewhere.
	INHERIT,
}
##
## Add this component to a scene that should participate in the network
## session as a runtime-spawned entity (NPC, enemy, pickup, drop) or as
## a preplaced level entity (interactable, environment piece).
##
## [br][br]
## [b]Lifecycle (this is load-bearing):[/b]
## [br]- The component connects [signal Node.tree_entered] of [member
##   Node.owner] to [code]_on_owner_tree_entered[/code] in
##   [code]_validate_editor[/code], with [constant
##   Object.CONNECT_PERSIST]. The connection is saved into the
##   [code].tscn[/code] file.
## [br]- On the client, the engine instantiates the spawned scene
##   without running any orchestrator code; the persisted signal
##   connection is the only mechanism that fires before children's
##   [code]_enter_tree[/code]. Authority must be set there to avoid
##   stale-authority reads in sibling components.
## [br]- If the connection is missing at runtime (e.g., script attached
##   programmatically, scene not re-saved), the component asserts and
##   instructs the user to reload the entity scene.
##
## [br][br]
## [b]Authority policy:[/b]
## [br]- [code]SERVER[/code] (default): authority is [code]1[/code].
##   Suitable for NPCs, enemies, and most preplaced entities.
## [br]- [code]FIXED_PEER[/code]: authority is [member authority_peer].
##   Set by the spawning code before the spawn replicates.
## [br]- [code]INHERIT[/code]: leave authority untouched.
##
## [br][br]
## [b]Hydrate timing:[/b] [SaveComponent] hydration runs
## automatically from [method _on_owner_tree_entered] on the
## server when a [SaveComponent] sibling with an assigned
## [member SaveComponent.database] is present. [SpawnerComponent]
## never hydrates from [code]_ready[/code] because that runs
## after sibling components have already entered the tree,
## breaking ordering contracts (e.g., [TPComponent] reading
## [code]current_scene_path[/code]).
##
## [codeblock]
## # On a preplaced rock with a SaveComponent, save state is
## # loaded by the SaveComponent itself on _ready. SpawnerComponent
## # only handles authority + SceneSynchronizer registration.
##
## # On a runtime-spawned enemy:
## #     entity.spawn_under(parent)
## # The entity hydrates automatically on tree entry.
## [/codeblock]

## Emitted on the server after the entity has been configured.
signal spawned

## Emitted right before the entity is despawned via
## [method despawn], with the despawn reason.
signal despawning(reason: StringName)

## Emitted from [method _exit_tree] after teardown.
signal despawned

## How multiplayer authority is decided.
@export var authority_policy: AuthorityPolicy = (
	AuthorityPolicy.SERVER
)

## Peer id used when [member authority_policy] is [code]FIXED_PEER[/code].
@export var authority_peer: int = 1

## When [code]true[/code], a [SpawnSynchronizer] is built on tree entry
## that captures sibling synchronizers' properties as spawn-only state.
## Off by default - most preplaced entities have all initial state in
## the scene file. Turn on for runtime-spawned entities that need to
## carry initial state to remote clients.
@export var build_spawn_sync: bool = false

## When [code]true[/code], the entity registers itself with the
## enclosing [SceneSynchronizer] so per-peer scene visibility filters
## apply. Default on; turn off for entities that should be globally
## visible regardless of scene membership.
@export var auto_track_in_scene: bool = true


var _dbg: NetwHandle = Netw.dbg.handle(self)
var _spawn_sync: SpawnSynchronizer


## Returns the [SpawnerComponent] under the unique name
## [code]%SpawnerComponent[/code] from [param node], or [code]null[/code].
static func unwrap(node: Node) -> SpawnerComponent:
	return node.get_node_or_null("%SpawnerComponent")


## Parses the multiplayer authority from a node name formatted as
## [code]username|peer_id[/code].
## Returns [param peer_id] as an [int], or [code]0[/code] if the name does
## not contain the separator.
static func parse_authority(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0


func _init() -> void:
	name = "SpawnerComponent"
	unique_name_in_owner = true


func _ready() -> void:
	if Engine.is_editor_hint():
		_validate_editor()
		return
	_dbg.trace("_ready for %s", [owner.name if owner else "<no owner>"])
	
	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s` "
		+ "(via CONNECT_PERSIST). Reload the entity scene to wire "
		+ "the connection automatically." % [owner.name, "_on_owner_tree_entered"]
	)
	spawned.emit()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	despawned.emit()


func _validate_editor() -> void:
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(
			_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST
		)


## Runs in the unique window after [member Node.owner] enters the tree
## but before any sibling component's [code]_enter_tree[/code] fires.
## Subclasses override the policy hooks
## ([method _apply_authority], [method _setup_spawn_sync],
## [method _register_with_scene]) to specialize behavior.
func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
	_dbg.trace("Entity '%s' entering tree.", [owner.name])
	_apply_authority()
	_hydrate_save()
	_setup_spawn_sync()
	_register_with_scene()


## Applies [member authority_policy] to [member Node.owner].
##
## Override in subclasses to implement policy-specific logic
## (for [SpawnerPlayerComponent] this parses [code]username|peer_id[/code]
## from the node name).
func _apply_authority() -> void:
	match authority_policy:
		AuthorityPolicy.SERVER:
			owner.set_multiplayer_authority(
				MultiplayerPeer.TARGET_PEER_SERVER
			)
		AuthorityPolicy.FIXED_PEER:
			owner.set_multiplayer_authority(authority_peer)
		AuthorityPolicy.INHERIT:
			pass


# Server-only: hydrates a [SaveComponent] sibling from the database,
# fetching the entity record by ID. Runs before spawn-sync so
# captured spawn properties include hydrated values.
func _hydrate_save() -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	var save: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save:
		save.hydrate_from_db()


## Builds a [SpawnSynchronizer] when [member build_spawn_sync] is on.
##
## Override in subclasses to gate on additional conditions (the
## player flow only builds when [member Node.owner] is server-owned).
func _setup_spawn_sync() -> void:
	if not build_spawn_sync:
		return
	if not _spawn_sync:
		_spawn_sync = SpawnSynchronizer.new(self)
	_spawn_sync.config_spawn_properties(self)
	_spawn_sync.set_multiplayer_authority(
		MultiplayerPeer.TARGET_PEER_SERVER
	)


## Registers the entity with the enclosing [SceneSynchronizer] so
## per-peer scene visibility filters apply.
func _register_with_scene() -> void:
	if not auto_track_in_scene:
		return
	var scene := MultiplayerTree.scene_for_node(self)
	if not scene:
		_dbg.trace(
			"No enclosing MultiplayerScene for '%s'; skipping "
			+ "SceneSynchronizer track.", [owner.name]
		)
		return
	scene.synchronizer.track_node(owner)


## Server-only. Duplicates [member Node.owner] as a sibling of
## [param parent] (or [member Node.owner]'s own parent when
## [param parent] is [code]null[/code]) and adds it to the tree.
##
## All configuration (authority, DB hydration, spawn-sync,
## [SceneSynchronizer] tracking) runs automatically in
## [method _on_owner_tree_entered] when the copy enters the tree.
func spawn_under(parent: Node = null) -> Node:
	assert(
		not multiplayer or multiplayer.is_server(),
		"spawn_under is server-only"
	)
	var p := parent if parent else owner.get_parent()
	var copy: Node = load(owner.scene_file_path).instantiate()
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
