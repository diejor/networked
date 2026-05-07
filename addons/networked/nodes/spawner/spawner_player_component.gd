@tool
class_name SpawnerPlayerComponent
extends SpawnerComponent
## The authoritative bridge between a connecting peer and their in-world
## representation.
##
## A specialization of [SpawnerComponent] for the player flow: the entity
## scene is instantiated by the [JoinPayload] orchestrator,
## [member username] participates in the [code]username|peer_id[/code]
## node-name convention, and authority is parsed from the node name on
## tree entry. Spawn-only state replication is built unconditionally.
## [codeblock]
## # Retrieve from any node in the player scene:
## var spawner := SpawnerPlayerComponent.unwrap(player_node)
## if spawner:
##     print(spawner.username)
## [/codeblock]

## Emitted on the server when a peer requests to join.
signal player_joined(join_payload: JoinPayload)
## Emitted each time a client-owned [MultiplayerSynchronizer] delivers a
## delta update.
signal client_synchronized

## Controls how multiplayer authority is assigned to the spawned player.
enum AuthorityMode {
	## Authority is set to the player peer ID. Default. Use for
	## client-authoritative movement.
	CLIENT,
	## Authority stays at [code]1[/code]. Use when the server drives
	## all simulation.
	SERVER_AUTHORITATIVE,
}

## How multiplayer authority is assigned to the player node on tree entry.
@export var authority_mode: AuthorityMode = AuthorityMode.CLIENT

## The username of the player associated with this component.
var username: String = ""


## The [MultiplayerSynchronizer] used for initial spawn state replication.
##
## Lazily constructed on first access. Always returns the
## [code]%SpawnSynchronizer[/code] unique-name node so subsequent
## accesses return the same instance.
var spawn_sync: SpawnSynchronizer:
	get:
		if not spawn_sync:
			spawn_sync = SpawnSynchronizer.new(self)
		return %SpawnSynchronizer


## Returns the [SpawnerPlayerComponent] with unique name
## [code]%SpawnerPlayerComponent[/code] from [param node],
## or [code]null[/code].
static func unwrap(node: Node) -> SpawnerPlayerComponent:
	return node.get_node_or_null("%SpawnerPlayerComponent")


func _init() -> void:
	name = "SpawnerPlayerComponent"
	unique_name_in_owner = true
	build_spawn_sync = true
	auto_track_in_scene = false
	if not player_joined.is_connected(_on_player_joined):
		player_joined.connect(_on_player_joined)


func _ready() -> void:
	if Engine.is_editor_hint():
		_validate_editor()
		return

	_dbg.trace("_ready for %s", [owner.name])

	if is_multiplayer_authority():
		var mt := MultiplayerTree.resolve(self)
		if mt:
			mt.local_player = self.owner

	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)

	if username.is_empty():
		owner.process_mode = Node.PROCESS_MODE_DISABLED
		owner.visible = false
		if not multiplayer.is_server():
			_dbg.trace(
				"Freeing spawner node `%s` because we are in client.",
				[owner.name]
			)
			owner.queue_free()
		SynchronizersCache.sync_only_server(owner)

	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise \
the authority will not be set correctly. Reload the player scene to connect \
automatically." % [owner.name, _on_owner_tree_entered]
	)

	if not multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	var tp_layer := get_tp_layer()
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		_dbg.info(
			"Local player %s ready. Playing teleport transition.", [username]
		)
		tp_layer.teleport_in()

	spawned.emit()


# Override: parse authority from `username|peer_id` node name when
# `authority_mode == CLIENT`. Skip when owner already differs from peer 1
# (a previous frame already configured authority).
func _apply_authority() -> void:
	if owner.get_multiplayer_authority() != 1:
		return

	assert(owner.name != "|")

	var authority := SpawnerComponent.parse_authority(owner.name)
	if authority != 0 and authority_mode == AuthorityMode.CLIENT:
		_dbg.debug(
			"Setting authority for %s to %d", [owner.name, authority]
		)
		owner.set_multiplayer_authority(authority)


# Override: the player flow always builds a SpawnSynchronizer; the
# `spawn_sync` getter ensures one exists.
func _setup_spawn_sync() -> void:
	var existing := owner.get_node_or_null(
		"SpawnerPlayerComponent/SpawnSynchronizer"
	)
	_dbg.debug(
		"_on_owner_tree_entered: existing SpawnSynchronizer=%s, "
		+ "spawn_sync var=%s", [existing, spawn_sync]
	)
	spawn_sync.config_spawn_properties(self)
	spawn_sync.set_multiplayer_authority(
		MultiplayerPeer.TARGET_PEER_SERVER
	)


# Override: the player flow registers via MultiplayerScene.add_player,
# which calls SceneSynchronizer.track_node. Let the default
# SpawnerComponent registration handle the track.
func _register_with_scene() -> void:
	super()


## Server-only. Duplicates the owner scene, sets the
## [code]username|peer_id[/code] node name and [member username]
## for DB hydration, then calls [method MultiplayerScene.add_player]
## on [param scene].
##
## All remaining configuration (authority, DB hydration, spawn-sync)
## runs automatically in [method _on_owner_tree_entered] when the
## copy enters the tree.
func spawn_player(
	jp: JoinPayload, scene: MultiplayerScene
) -> Node:
	assert(multiplayer.is_server())
	var copy: Node = load(owner.scene_file_path).instantiate()
	copy.name = "%s|%d" % [jp.username, jp.peer_id]
	var copy_spawner: SpawnerPlayerComponent = (
		copy.get_node_or_null("%SpawnerPlayerComponent")
	)
	if copy_spawner:
		copy_spawner.username = jp.username
	scene.add_player(copy)
	return copy


func _on_player_joined(join_payload: JoinPayload) -> void:
	var ctx := get_context()
	if not ctx:
		return

	var slot := ctx.tree.get_spawn_slot(
		join_payload.spawner_component_path
	)
	if not slot.is_valid():
		_dbg.error(
			"Player join failed: no active scene for '%s'.",
			[join_payload.spawner_component_path.get_scene_name()],
			func(m): push_error(m)
		)
		return

	# Resolve target scene: prefer TP-routed scene from saved
	# state, fall back to the slot's default scene.
	var scene := _resolve_target_scene(join_payload, slot)

	if scene:
		spawn_player(join_payload, scene)
	elif slot.is_valid():
		var copy: Node = owner.duplicate()
		copy.name = "%s|%d" % [join_payload.username, join_payload.peer_id]
		var copy_spawner: SpawnerPlayerComponent = (
			copy.get_node_or_null("%SpawnerPlayerComponent")
		)
		if copy_spawner:
			copy_spawner.username = join_payload.username
		slot.place_player(copy)
	else:
		_dbg.error("Cannot place player: no scene available.", [])


func _resolve_target_scene(
	jp: JoinPayload, slot: SpawnSlot
) -> MultiplayerScene:
	var ctx := get_context()
	if not ctx:
		return null

	var level_save: SaveComponent = (
		owner.get_node_or_null("%SaveComponent") as SaveComponent
	)
	if (
		level_save
		and level_save.database
		and not level_save.table_name.is_empty()
	):
		var entity := level_save.database.table(
			level_save.table_name
		).fetch(StringName(jp.username))
		if entity:
			var path: String = entity.get_value(
				&"current_scene_path", ""
			)
			if not path.is_empty():
				var scene_mgr := (
					ctx.services.get_scene_manager()
				)
				if scene_mgr:
					var name := StringName(
						path.get_file().get_basename()
					)
					var scene := (
						scene_mgr.active_scenes.get(name)
					)
					if scene:
						return scene

	if slot.has_scene():
		return slot.get_scene()

	return null


func _on_peer_disconnected(peer_id: int) -> void:
	if (multiplayer and multiplayer.is_server()
			and get_multiplayer_authority() == peer_id):
		_dbg.info(
			"Peer %d disconnected. Despawning owned player %s.",
			[peer_id, owner.name]
		)
		var opts := DespawnOpts.new()
		opts.reason = &"peer_disconnected"
		despawn(opts)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	if is_multiplayer_authority():
		var mt := MultiplayerTree.resolve(self)
		if mt and mt.local_player == self:
			mt.local_player = null
	super()
