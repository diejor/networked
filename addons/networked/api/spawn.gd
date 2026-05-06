## Player and entity spawn pipeline.
##
## [NetwSpawn] creates and tears down networked nodes. It handles the
## full lifecycle: gathering player data from a database, applying it to
## instantiated scenes, placing nodes into a [MultiplayerScene] for
## replication, and tearing them down cleanly.
##
## Access via [member Netw.spawn].
##
## [br][br]
## [b]Custom Spawn Functions[/b]: the most common path when using
## Godot's [MultiplayerSpawner]:
## [codeblock]
## func my_spawn_function(raw: Variant) -> Node:
##     var payload := SpawnPayload.from_variant(raw)
##     var player := player_scene.instantiate()
##     Netw.spawn.configure(payload, player)
##     return player
## [/codeblock]
##
## [br]
## [b]Full Player Lifecycle[/b]: when you control the entire pipeline:
## [codeblock]
## var span := Netw.spawn.begin_join(join_payload, authority_mode)
## var payload := Netw.spawn.gather(join_payload, db, &"players", {}, span)
## var player := player_scene.instantiate()
## Netw.spawn.configure(payload, player, null, span)
## Netw.spawn.place(player, scene, span)
## [/codeblock]
## Each stage accepts an optional [NetSpan] for causal tracing.
##
## [br][br]
## [b]Non-Player Entities[/b]: server-side single call:
## [codeblock]
## var goblin := Netw.spawn.spawn_entity(
##     goblin_scene, %EnemyContainer,
##     EntityPayload.new(&"goblin", {hp = 30}),
## )
## [/codeblock]
##
## [br]
## [b]Despawning[/b]: flushes save data and removes the node:
## [codeblock]
## Netw.spawn.despawn(node)
## [/codeblock]
##
## [br]
## [b]Important:[/b] Save data must be written [i]before[/i] the node
## enters the scene tree. [method configure] and [method spawn_entity]
## handle this automatically; if you use the lower-level
## [method configure_node] or [method hydrate_save] directly, hydrate
## first, then add to the tree.
class_name NetwSpawn
extends RefCounted


## Creates a [NetSpan] for a player join operation, pre-filled with
## peer metadata, and logs the join event.
##
## [br]
## [br] [param join_payload] carries the joining peer's connection details.
## [br] [param authority_mode] is the [member SpawnerComponent.authority_mode]
## for the owning spawner.
## [br] [param context] is used to associate the span with a
## [MultiplayerTree]. When [code]null[/code], the span is tree-agnostic.
static func begin_join(
	join_payload: JoinPayload,
	authority_mode: int,
	context: Object = null,
) -> NetSpan:
	var span := Netw.dbg.span(
		context, "player_join",
		{
			"username": join_payload.username,
			"peer_id": join_payload.peer_id,
			"authority_mode": authority_mode,
		},
	)
	Netw.dbg.info(
		"Player joined: %s (ID: %d)",
		[join_payload.username, join_payload.peer_id],
	)
	return span


## Gathers spawn data for [param join_payload].
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
	join_payload: JoinPayload,
	db: NetwDatabase = null,
	table_name: StringName = &"",
	extras: Dictionary = {},
	span: NetSpan = null,
) -> SpawnPayload:
	var save_record: Dictionary = {}
	if db and not table_name.is_empty():
		var entity := db.table(table_name).fetch(StringName(join_payload.username))
		if entity:
			save_record = entity.to_dict()
	var payload := SpawnPayload.new(
		join_payload.username, join_payload.peer_id, save_record, extras
	)
	if span:
		span.step("gather", {
			"has_db": db != null,
			"has_save": not save_record.is_empty(),
		})
	return payload


## Generic, entity-agnostic configure primitive.
##
## Applies [param opts] to [param node]: optional name override.
## Authority is intentionally
## not applied here; authority is set in
## [code]_on_owner_tree_entered[/code] by [EntityComponent] / its
## subclasses to satisfy Godot's tree-entry ordering.
## [br][br]
## Call this from a custom [code]spawn_function[/code] when you want
## the addon to enforce the spawn invariants without committing to
## the player flow.
## [br][br]
## Records a [code]"configure_node"[/code] step on [param span] when
## provided.
static func configure_node(
	node: Node,
	opts: ConfigureOpts = null,
	span: NetSpan = null,
) -> void:
	if opts == null:
		opts = ConfigureOpts.new()
	if not opts.name_override.is_empty():
		node.name = opts.name_override
	if span:
		span.step("configure_node", {
			"class_id": String(opts.class_id),
			"authority_policy": opts.authority_policy,
		})


## Hydrates a [SaveComponent] sibling of [param node] from
## [param save_state] when both are present.
##
## A no-op when [param node] has no [SaveComponent] child
## or when [param save_state] is empty (the [SaveComponent] will
## seed from scene defaults on its own).
## [br][br]
## Must be called BEFORE [param node] enters the scene tree so that
## any sibling component reading save state in [code]_enter_tree[/code]
## or [code]_ready[/code] (such as [TPComponent] reading
## [code]current_scene_path[/code]) sees a hydrated state.
## [br][br]
## Records a [code]"hydrate_save"[/code] step on [param span] when
## provided.
static func hydrate_save(
	node: Node,
	save_state: Dictionary,
	span: NetSpan = null,
) -> void:
	var save: SaveComponent = node.get_node_or_null("%SaveComponent")
	if save:
		save.hydrate(save_state)
	if span:
		span.step("hydrate_save", {
			"has_save": save != null,
			"record_size": save_state.size(),
		})


## Configures an existing [param node] from [param payload] for the
## player flow.
##
## Sets [member SpawnerComponent.username] and the
## [code]username|peer_id[/code] node name, hydrates the
## [SaveComponent] via [method hydrate_save], and runs the generic
## [method configure_node] checks.
## [br][br]
## Records a [code]"configure"[/code] step on [param span] when
## provided.
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
	var opts := ConfigureOpts.new()
	if not payload.username.is_empty() and payload.peer_id != 0:
		opts.name_override = "%s|%s" % [payload.username, payload.peer_id]
	opts.class_id = &"player"
	configure_node(node, opts)
	hydrate_save(node, payload.save_state)
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


## Spawns [param scene] under [param parent] for non-player entities.
##
## [b]Server-only.[/b] Uses the engine's auto-tracking spawn path -
## no [code]spawn_function[/code] is required. The covering
## [MultiplayerSpawner] (validated in debug builds) replicates the
## spawn to every peer in the relevant visibility set.
## [br][br]
## Steps:
## [br]1. Asserts [param parent] is on the multiplayer authority and
##   that some ancestor [MultiplayerSpawner] covers it with [param
##   scene] in its [code]_spawnable_scenes[/code]
##   ([method _assert_replicable]).
## [br]2. Instantiates [param scene].
## [br]3. Runs [method configure_node] for invariant checks.
## [br]4. Hydrates [SaveComponent] from [param payload] when both are
##   present.
## [br]5. Adds the node to [param parent]; the engine handles spawn
##   replication.
## [br][br]
## Returns the local instance so the caller can attach signals or
## extra state. Authority for replicated initial state arrives at
## remote clients via spawn-only properties (see [SpawnSynchronizer]
## and [member EntityComponent.build_spawn_sync]).
## [codeblock]
## # Server-side, in any level script:
## var goblin := Netw.spawn.spawn_entity(
##     goblin_scene,
##     %EnemyContainer,
##     EntityPayload.new(&"goblin", {hp = 30}),
## )
## goblin.died.connect(_on_goblin_died)
## [/codeblock]
static func spawn_entity(
	scene: PackedScene,
	parent: Node,
	payload: EntityPayload = null,
	opts: ConfigureOpts = null,
) -> Node:
	assert(
		parent.multiplayer and parent.multiplayer.is_server(),
		"NetwSpawn.spawn_entity must be called on the server."
	)
	assert(
		_assert_replicable(parent, scene.resource_path),
		_replicable_error(parent, scene.resource_path)
	)

	var span := Netw.dbg.span(parent, "spawn_entity", {
		"scene": scene.resource_path,
		"parent": parent.name,
		"class_id": String(payload.class_id) if payload else "",
	})

	var node: Node = scene.instantiate()
	span.step("instantiate")

	if opts == null:
		opts = ConfigureOpts.new()
		if payload and not payload.class_id.is_empty():
			opts.class_id = payload.class_id

	configure_node(node, opts, span)

	if payload and not payload.save_state.is_empty():
		hydrate_save(node, payload.save_state, span)

	parent.add_child(node)
	span.step("add_child", {"path": str(node.get_path())})
	span.end()
	return node


## Tears down a networked node in the canonical order: emit
## [signal EntityComponent.despawning], flush [SaveComponent], revert
## authority to the server, then [method Node.queue_free].
##
## [br][br]
## [b]Server-only.[/b] Asserts when called on a non-server peer.
## Replication of the despawn to remote clients is handled by the
## owning [MultiplayerSpawner] (auto-tracking) once the node leaves
## the tree.
## [br][br]
## [b]Despawn is infallible from the caller's perspective.[/b] A
## [SaveComponent.flush] failure is logged at error level and the
## despawn proceeds. Callers that need transactional save semantics
## should flush themselves first and pass [code]flush_save: false[/code]
## in [param opts].
static func despawn(node: Node, opts: DespawnOpts = null) -> void:
	if opts == null:
		opts = DespawnOpts.new()

	if not is_instance_valid(node):
		Netw.dbg.warn("despawn() called with freed node; ignoring.", [])
		return

	assert(
		node.multiplayer and node.multiplayer.is_server(),
		"NetwSpawn.despawn must be called on the server."
	)

	var span := Netw.dbg.span(node, "despawn", {
		"reason": String(opts.reason),
		"node": node.name,
	})

	var entity := EntityComponent.unwrap(node)
	if entity == null:
		entity = SpawnerComponent.unwrap(node) as EntityComponent
	if entity:
		entity.despawning.emit(opts.reason)

	if opts.flush_save:
		var save: SaveComponent = node.get_node_or_null("%SaveComponent")
		if save:
			var err := save.flush()
			if err != OK:
				Netw.dbg.error(
					"despawn: SaveComponent.flush failed for '%s' "
					+ "with %s; proceeding.",
					[node.name, error_string(err)],
					func(m): push_error(m)
				)
			elif span:
				span.step("flush_save")

	if node.get_multiplayer_authority() != MultiplayerPeer.TARGET_PEER_SERVER:
		node.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		if span:
			span.step("authority_reverted")

	if opts.defer_free:
		node.queue_free.call_deferred()
	else:
		node.queue_free()

	if span:
		span.step("queue_free", {"defer": opts.defer_free})
		span.end()


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
	assert(
		_assert_replicable(scene.level, player.scene_file_path),
		_replicable_error(scene.level, player.scene_file_path)
	)
	scene.synchronizer.track_node(player)
	scene.level.add_child(player)
	player.owner = scene.level
	if span:
		span.step("place", {"scene": scene.level.name})
		span.end()


# Returns [code]true[/code] when [param parent] has a [MultiplayerSpawner]
# ancestor whose [member MultiplayerSpawner.spawn_path] covers [param parent]
# and whose [code]_spawnable_scenes[/code] includes
# [param scene_file_path].
static func _assert_replicable(
	parent: Node, scene_file_path: String
) -> bool:
	if scene_file_path.is_empty():
		return true
	return _find_covering_spawner(parent, scene_file_path) != null


# Walks ancestors of [param parent] looking for a [MultiplayerSpawner]
# whose spawn_path covers [param parent] and whose
# [code]_spawnable_scenes[/code] contains [param scene_file_path].
#
# Returns the matching [MultiplayerSpawner] or [code]null[/code].
static func _find_covering_spawner(
	parent: Node, scene_file_path: String
) -> MultiplayerSpawner:
	var node: Node = parent
	while is_instance_valid(node):
		for child in node.get_children():
			if child is MultiplayerSpawner:
				var spawner: MultiplayerSpawner = child
				var spawn_root := spawner.get_node_or_null(spawner.spawn_path)
				if not spawn_root:
					continue
				if (
					spawn_root != parent
					and not spawn_root.is_ancestor_of(parent)
				):
					continue
				if scene_file_path in _get_spawnable_scenes(spawner):
					return spawner
		node = node.get_parent()
	return null


static func _get_spawnable_scenes(spawner: MultiplayerSpawner) -> Array[String]:
	var scenes_path: Array[String]
	for scene_idx in spawner.get_spawnable_scene_count():
		scenes_path.append(spawner.get_spawnable_scene(scene_idx))
	return scenes_path


# Builds a copy-pasteable error message for the replicability assertion.
static func _replicable_error(
	parent: Node, scene_file_path: String
) -> String:
	var parent_path := (
		str(parent.get_path()) if is_instance_valid(parent) else "<freed>"
	)
	return (
		"Cannot replicate spawn into '%s': no MultiplayerSpawner "
		+ "ancestor has spawn_path covering '%s' and '%s' in "
		+ "_spawnable_scenes. Add a MultiplayerSpawner whose spawn_path "
		+ "is an ancestor of '%s', and register '%s' in its "
		+ "_spawnable_scenes."
	) % [
		parent_path, parent_path, scene_file_path,
		parent_path, scene_file_path,
	]
