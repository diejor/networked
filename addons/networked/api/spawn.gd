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


## Creates a [NetSpan] for a player join operation, pre-filled with
## peer metadata, and logs the join event.
##
## [param client_data] carries the joining peer's connection details.
## [param authority_mode] is the [member SpawnerComponent.authority_mode]
## for the owning spawner.
## [param context] is used to associate the span with a
## [MultiplayerTree]. When [code]null[/code], the span is tree-agnostic.
static func begin_join(
	client_data: MultiplayerClientData,
	authority_mode: int,
	context: Object = null,
) -> NetSpan:
	var span := Netw.dbg.span(
		context, "player_join",
		{
			"username": client_data.username,
			"peer_id": client_data.peer_id,
			"authority_mode": authority_mode,
		},
	)
	Netw.dbg.info(
		"Player joined: %s (ID: %d)",
		[client_data.username, client_data.peer_id],
	)
	return span


## Gathers spawn data for [param client_data].
##
## When [param db] and [param table_name] are provided, loads the player's
## entity record directly from the database via
## [method NetwDatabase.TableRepository.fetch]. [param extras] is merged into
## [member SpawnPayload.extras] and serialized with
## [method SpawnPayload.to_variant].
##
## Records a [code]"gather"[/code] step on [param span] when provided.
## [br][br]
## [b]Note:[/b] Values in [param extras] must be Godot-serializable if
## they travel through a [MultiplayerSpawner].
static func gather(
	client_data: MultiplayerClientData,
	db: NetwDatabase = null,
	table_name: StringName = &"",
	extras: Dictionary = {},
	span: NetSpan = null,
) -> SpawnPayload:
	var save_record: Dictionary = {}
	if db and not table_name.is_empty():
		var entity := db.table(table_name).fetch(StringName(client_data.username))
		if entity:
			save_record = entity.to_dict()
	var payload := SpawnPayload.new(
		client_data.username, client_data.peer_id, save_record, extras
	)
	if span:
		span.step("gather", {
			"has_db": db != null,
			"has_save": not save_record.is_empty(),
		})
	return payload


## Configures an existing [param node] from [param payload].
##
## Sets [member SpawnerComponent.username] and the node name, hydrates
## the [SaveComponent] via [method SaveComponent.hydrate], and
## applies any extra state carried in [param payload].
##
## Records a [code]"configure"[/code] step on [param span] when provided.
## [br][br]
## Works on both server and client - call this inside your
## [code]spawn_function[/code] after instantiating the node.
## [br][br]
## [param caller] is kept for API compatibility but no longer used.
static func configure(
	payload: SpawnPayload,
	node: Node,
	caller: Node = null,
	span: NetSpan = null,
) -> void:
	var spawner: SpawnerComponent = SpawnerComponent.unwrap(node)
	if spawner and not payload.username.is_empty():
		spawner.username = String(payload.username)
	if not payload.username.is_empty() and payload.peer_id != 0:
		node.name = "%s|%s" % [payload.username, payload.peer_id]
	
	var save: SaveComponent = node.get_node_or_null("%SaveComponent")
	if save:
		save.hydrate(payload.save_state)
	
	assert(
		MultiplayerClientData.parse_authority(node.name) != 0,
		"Node name must follow 'username|peer_id' after configure()."
	)
	var has_sync := false
	for child in node.get_children():
		if child is MultiplayerSynchronizer:
			has_sync = true
			break
	if not has_sync:
		Netw.dbg.warn(
			"Node '%s' has no MultiplayerSynchronizer children. "
			+ "Scene visibility may not work correctly.", [node.name]
		)
	if span:
		span.step("configure", {
			"username": String(payload.username),
			"has_save": not payload.save_state.is_empty(),
		})


## Creates a player [Node] from [param scene_template] and applies
## [param payload] via [method configure].
##
## Equivalent to [code]scene_template.instantiate()[/code] followed by
## [method configure]. Returns the configured node (not yet in the scene
## tree).
##
## Records an [code]"instantiate"[/code] step on [param span] when
## provided. The span is not forwarded to [method configure] to avoid
## duplicate step recording - use [method configure] directly with a span
## for per-stage granularity.
static func instantiate(
	payload: SpawnPayload,
	scene_template: PackedScene,
	caller: Node = null,
	span: NetSpan = null,
) -> Node:
	var player: Node = scene_template.instantiate()
	configure(payload, player, caller)
	if span:
		span.step("instantiate")
	return player


## Adds [param player] to [param scene] and registers it with the
## [SceneSynchronizer] for visibility management.
##
## Must be called before the player enters the tree so the
## [SceneSynchronizer] detects [signal Node.tree_entered].
##
## Records a [code]"place"[/code] step on [param span] and closes the
## span with [method NetSpan.end] when provided.
static func place(
	player: Node, scene: MultiplayerScene, span: NetSpan = null
) -> void:
	scene.synchronizer.track_player(player)
	scene.level.add_child(player)
	player.owner = scene.level
	if span:
		span.step("place", {"scene": scene.level.name})
		span.end()
