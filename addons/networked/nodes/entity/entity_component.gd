@tool
class_name EntityComponent
extends NetwComponent
## Wakeup contract for any networked entity that is not a player.
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
## [b]Hydrate timing:[/b] [SaveComponent] hydration is the
## responsibility of the spawn orchestrator
## ([NetwSpawn.spawn_entity] or a custom [code]spawn_function[/code]
## via [method NetwSpawn.hydrate_save]). [EntityComponent] never
## hydrates from [code]_ready[/code] because that runs after sibling
## components have already entered the tree, breaking ordering
## contracts (e.g., [TPComponent] reading
## [code]current_scene_path[/code]).
##
## [codeblock]
## # On a preplaced rock with a SaveComponent, save state is
## # loaded by the SaveComponent itself on _ready. EntityComponent
## # only handles authority + SceneSynchronizer registration.
##
## # On a runtime-spawned enemy:
## #     Netw.spawn.spawn_entity(scene, level, payload)
## # The orchestrator hydrates BEFORE add_child; EntityComponent
## # picks up authority on owner.tree_entered.
## [/codeblock]

## Emitted on the server after the entity has been configured.
signal spawned

## Emitted right before the entity is despawned via
## [method NetwSpawn.despawn], with the despawn reason.
signal despawning(reason: StringName)

## Emitted from [method _exit_tree] after teardown.
signal despawned

## How multiplayer authority is decided.
@export var authority_policy: ConfigureOpts.AuthorityPolicy = (
	ConfigureOpts.AuthorityPolicy.SERVER
)

## Peer id used when [member authority_policy] is [code]FIXED_PEER[/code].
@export var authority_peer: int = 1

## Stable identifier for this entity class. Used for save-table routing
## and debug labels. Optional.
@export var class_id: StringName

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


## Returns the [EntityComponent] under the unique name
## [code]%EntityComponent[/code] from [param node], or [code]null[/code].
static func unwrap(node: Node) -> EntityComponent:
	return node.get_node_or_null("%EntityComponent")


func _init() -> void:
	name = "EntityComponent"
	unique_name_in_owner = true


func _ready() -> void:
	if Engine.is_editor_hint():
		_validate_editor()
		return
	_dbg.trace("_ready for %s", [owner.name if owner else "<no owner>"])

	assert(
		owner != null,
		"EntityComponent must be a child of an entity scene root."
	)
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
	_setup_spawn_sync()
	_register_with_scene()


## Applies [member authority_policy] to [member Node.owner].
##
## Override in subclasses to implement policy-specific logic
## (for [SpawnerComponent] this parses [code]username|peer_id[/code]
## from the node name).
func _apply_authority() -> void:
	match authority_policy:
		ConfigureOpts.AuthorityPolicy.SERVER:
			owner.set_multiplayer_authority(
				MultiplayerPeer.TARGET_PEER_SERVER
			)
		ConfigureOpts.AuthorityPolicy.FIXED_PEER:
			owner.set_multiplayer_authority(authority_peer)
		ConfigureOpts.AuthorityPolicy.INHERIT:
			pass


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
