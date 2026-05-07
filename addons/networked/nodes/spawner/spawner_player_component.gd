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

## The username of the player associated with this component.
var username: String = ""


## Returns the [SpawnerPlayerComponent] with unique name
## [code]%SpawnerPlayerComponent[/code] from [param node],
## or [code]null[/code].
static func unwrap(node: Node) -> SpawnerPlayerComponent:
	return node.get_node_or_null("%SpawnerPlayerComponent")


func _init() -> void:
	name = "SpawnerPlayerComponent"
	unique_name_in_owner = true
	authority_mode = AuthorityMode.CLIENT
	if not player_joined.is_connected(_on_player_joined):
		player_joined.connect(_on_player_joined)


func _ready() -> void:
	if Engine.is_editor_hint():
		_validate_editor()
		return

	if is_multiplayer_authority():
		var mt := MultiplayerTree.resolve(self)
		if mt:
			mt.local_player = self.owner

	if not multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	if (
		not multiplayer.is_server()
		and is_multiplayer_authority()
		and is_inside_tree()
	):
		var ctx := Netw.ctx(self)
		if ctx:
			var tp_layer := ctx.services.get_tp_layer()
			if tp_layer:
				_dbg.info(
					"Local player %s ready. Playing teleport transition.",
					[username]
				)
				tp_layer.teleport_in()

	super._ready()


# Override: derive entity_id from username instead of owner name.
func _resolve_identity() -> StringName:
	return StringName(username) if not username.is_empty() else &""


# Override: add username + current_scene_path as spawn-only properties.
func _populate_extra_spawn_properties(cfg: SceneReplicationConfig) -> void:
	var comp_path := owner.get_path_to(self)
	_add_spawn_property_into(cfg, NodePath(str(comp_path) + ":username"))

	var tp := owner.get_node_or_null("%TPComponent")
	if tp:
		var tp_path := owner.get_path_to(tp)
		_add_spawn_property_into(
			cfg, NodePath(str(tp_path) + ":current_scene_path")
		)


## Server-only. Duplicates the owner scene, sets the
## [code]username|peer_id[/code] node name and [member username]
## for DB hydration, then calls [method MultiplayerScene.add_player]
## on [param scene].
##
## All remaining configuration (authority, DB hydration, scene tracking)
## runs automatically in [method _on_owner_tree_entered] when the copy
## enters the tree.
func spawn_player(jp: JoinPayload, scene: MultiplayerScene) -> Node:
	assert(multiplayer.is_server())
	var copy := SpawnerComponent.instantiate_from(owner,
		func(c: SpawnerComponent) -> void:
			var pc := c as SpawnerPlayerComponent
			if pc:
				pc.username = jp.username
			c.owner.name = SpawnerComponent.format_name(
				jp.username, jp.peer_id
			)
	)
	scene.add_player(copy)
	return copy


func _on_player_joined(join_payload: JoinPayload) -> void:
	var ctx := Netw.ctx(self)
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

	var scene := _resolve_target_scene(join_payload, slot)
	if not scene:
		_dbg.error("Cannot place player: no scene available.", [])
		return
	spawn_player(join_payload, scene)


func _resolve_target_scene(
	jp: JoinPayload, slot: SpawnSlot
) -> MultiplayerScene:
	var ctx := Netw.ctx(self)
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
	super._exit_tree()
