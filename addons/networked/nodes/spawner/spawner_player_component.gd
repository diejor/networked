class_name SpawnerPlayerComponent
extends SpawnerComponent
## [SpawnerComponent] specialization for player entities.
##
## Tracks the peer represented by this player independently from
## [member authority_mode]. This lets server-authoritative player nodes
## still drive [member MultiplayerTree.local_player].
## [member username] and [member player_peer_id] are added as spawn-only
## properties.
##
## [codeblock]
## var s := SpawnerPlayerComponent.unwrap(player_node)
## if s:
##     print(s.username)
## [/codeblock]

## Emitted on the server when a peer requests to join.
signal player_joined(join_payload: JoinPayload)

## The username of the player associated with this component.
@export var username: String = ""

## Peer represented by this player entity. Assigned by the spawn flow.
var player_peer_id := 0


## Returns the [SpawnerPlayerComponent] with unique name
## [code]%SpawnerPlayerComponent[/code] from [param node],
## or [code]null[/code].
static func unwrap(node: Node) -> SpawnerPlayerComponent:
	return node.get_node_or_null("%SpawnerPlayerComponent")


## Returns the peer represented by this player entity. Falls back to the
## [code]username|peer_id[/code] owner name when [member player_peer_id]
## is [code]0[/code].
func get_player_peer_id() -> int:
	if player_peer_id != 0:
		return player_peer_id
	return SpawnerComponent.parse_authority(owner.name) if owner else 0


func _init() -> void:
	name = "SpawnerPlayerComponent"
	unique_name_in_owner = true
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
	if not player_joined.is_connected(_on_player_joined):
		player_joined.connect(_on_player_joined)


func _notification(what: int) -> void:
	super(what)
	if what != NOTIFICATION_PARENTED or Engine.is_editor_hint():
		return
	
	var entity := Netw.ctx(self).entity
	if not entity or not entity.owner:
		return
	
	var rel := entity.owner.get_path_to(self)
	entity.contribute_spawn_property("%s:username" % rel)
	entity.contribute_spawn_property("%s:player_peer_id" % rel)
	entity.scene_peer_id = get_player_peer_id()


func _on_owner_tree_entered() -> void:
	var entity := Netw.ctx(self).entity
	if entity:
		entity.scene_peer_id = get_player_peer_id()
	super._on_owner_tree_entered()


func _ready() -> void:
	super._ready()
	
	if _is_local_player() and not is_template:
		var mt := MultiplayerTree.resolve(self)
		if mt:
			mt.local_player = self.owner

	if not multiplayer.peer_disconnected.is_connected(_on_peer_disconnected):
		multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	if (
		not multiplayer.is_server()
		and _is_local_player()
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


# Override: derive entity_id from username instead of owner name.
func _resolve_identity() -> StringName:
	return StringName(username) if not username.is_empty() else &""


## Server-only. Spawns a player copy into [param scene] from
## [param jp]. Sets the node name to
## [code]username|peer_id[/code] and [member username] on the copy.
##
## [codeblock]
## var player := spawner.spawn_player(join_payload, scene)
## [/codeblock]
func spawn_player(jp: JoinPayload, scene: MultiplayerScene) -> Node:
	assert(multiplayer.is_server())
	var copy := SpawnerComponent.instantiate_from(owner,
		func(c: SpawnerComponent) -> void:
			var pc := c as SpawnerPlayerComponent
			if pc:
				pc.username = jp.username
				pc.player_peer_id = jp.peer_id
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
			and get_player_peer_id() == peer_id):
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

	if _is_local_player():
		var mt := MultiplayerTree.resolve(self)
		if mt and mt.local_player == owner:
			mt.local_player = null
	super._exit_tree()


func _is_local_player() -> bool:
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return false
	return get_player_peer_id() == multiplayer.get_unique_id()
