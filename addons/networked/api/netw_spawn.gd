## Spawn workflow primitives for the Networked addon.
##
## These helpers cover gathering player data, configuring a node from that
## data, and placing it into a [MultiplayerScene]. They are used both by
## the managed spawn path and by custom [code]spawn_function[/code]
## callbacks.
##
## [br][br]
## [b]Lifecycle invariants[/b]
##
## [br][br]
## A correctly spawned player scene follows three stages:
## [br]- [b]Gather[/b]: collect username, peer ID, and saved state from the
##   database.
## [br]- [b]Configure[/b]: write save state into [SaveComponent] before any
##   component that depends on it (such as [TPComponent]) reads from it.
## [br]- [b]Place[/b]: add the node to the scene tree under a
##   [SceneSynchronizer] so visibility is tracked from the first frame.
##
## [br][br]
## [b]Note:[/b] When using a [MultiplayerSpawner] with a custom
## [code]spawn_function[/code], only [method configure] is required.
## Instantiation and tree entry are handled by the engine.
## [codeblock]
## func my_spawn_fn(raw: Variant) -> Node:
##     var payload := SpawnPayload.from_variant(raw)
##     var player := player_scene.instantiate()
##     Netw.spawn.configure(payload, player)
##     return player
## [/codeblock]
class_name NetwSpawn
extends RefCounted


## Gathers spawn data for [param client_data].
##
## When [param db] and [param table_name] are provided, loads the player's
## entity record via [method SaveComponent.try_load_entity].
## [param extras] is merged into [member SpawnPayload.extras] and
## serialised with [method SpawnPayload.to_variant].
## [br][br]
## [b]Note:[/b] Values in [param extras] must be Godot-serializable if
## they travel through a [MultiplayerSpawner].
static func gather(
	client_data: MultiplayerClientData,
	db: NetworkedDatabase = null,
	table_name: StringName = &"",
	extras: Dictionary = {},
) -> SpawnPayload:
	var save_record: Dictionary = {}
	if db and not table_name.is_empty():
		save_record = SaveComponent.try_load_entity(
			db, table_name, StringName(client_data.username)
		)
	return SpawnPayload.new(
		client_data.username, client_data.peer_id, save_record, extras
	)


## Configures an existing [param node] from [param payload].
##
## Sets [member SpawnerComponent.username] and the node name, initialises
## the [SaveComponent] via [method SaveComponent.spawn_from_data], and
## applies any extra state carried in [param payload].
## [br][br]
## Works on both server and client — call this inside your
## [code]spawn_function[/code] after instantiating the node.
## [br][br]
## [param caller] is the spawner's owner node, used as the fallback source
## for default [SaveComponent] values when
## [member SpawnPayload.save_state] is empty.
static func configure(
	payload: SpawnPayload, node: Node, caller: Node = null
) -> void:
	var spawner: SpawnerComponent = SpawnerComponent.unwrap(node)
	if spawner and not payload.username.is_empty():
		spawner.username = String(payload.username)
	if not payload.username.is_empty() and payload.peer_id != 0:
		node.name = "%s|%s" % [payload.username, payload.peer_id]

	var save: SaveComponent = node.get_node_or_null("%SaveComponent")
	if save:
		save.spawn_from_data(payload.save_state, caller)


## Creates a player [Node] from [param scene_template] and applies
## [param payload] via [method configure].
##
## Equivalent to [code]scene_template.instantiate()[/code] followed by
## [method configure]. Returns the configured node (not yet in the scene
## tree).
static func instantiate(
	payload: SpawnPayload, scene_template: PackedScene, caller: Node = null
) -> Node:
	var player: Node = scene_template.instantiate()
	configure(payload, player, caller)
	return player


## Adds [param player] to [param scene] and registers it with the
## [SceneSynchronizer] for visibility management.
##
## Must be called before the player enters the tree so the
## [SceneSynchronizer] detects [signal Node.tree_entered].
static func place(player: Node, scene: MultiplayerScene) -> void:
	scene.synchronizer.track_player(player)
	scene.level.add_child(player)
	player.owner = scene.level
