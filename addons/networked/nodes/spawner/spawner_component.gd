@tool
class_name SpawnerComponent
extends NetwComponent
## The authoritative bridge between a connecting peer and their in-world
## representation.
##
## Add this node to your player scene. The component handles multiplayer
## authority setup and player teardown on disconnect.
## [br][br]
## [b]Spawn customisation:[/b] assign [member spawn_function] to override the
## default spawn sequence, or use [method SpawnContext.make_spawn_function]
## for ergonomic hook insertion.
## [br][br]
## [codeblock]
## # Retrieve from any node in the player scene:
## var spawner := SpawnerComponent.unwrap(player_node)
## if spawner:
##     print(spawner.username)
## [/codeblock]

## Emitted on the server when a peer requests to join.
signal player_joined(client_data: MultiplayerClientData)
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

## Override the default spawn sequence.
##
## When valid, called instead of [method SpawnContext.spawn_player] with
## signature [code]func(ctx: SpawnContext, data: MultiplayerClientData)[/code].
## Use [method SpawnContext.make_spawn_function] to wrap a hook.
var spawn_function: Callable

## The username of the player associated with this component.
var username: String = ""

var _dbg: NetwHandle = Netw.dbg.handle(self)


## The [MultiplayerSynchronizer] used for initial spawn state replication.
var spawn_sync: SpawnSynchronizer:
	get:
		if not spawn_sync:
			spawn_sync = SpawnSynchronizer.new(self)
		return %SpawnSynchronizer

## Allows the server to control visibility of client-authoritative players.
class SpawnSynchronizer extends MultiplayerSynchronizer:
	func _init(spawner: SpawnerComponent) -> void:
		name = "SpawnSynchronizer"
		unique_name_in_owner = true
		visibility_update_mode = \
			MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
		spawner.add_child(self)
		owner = spawner
		root_path = get_path_to(spawner.owner)

	## Builds a [SceneReplicationConfig] collecting spawn-only properties from
	## all client synchronizers of [param target_node].
	##
	## Marks each property as spawn-only
	## ([code]REPLICATION_MODE_NEVER[/code] with spawn enabled) so initial
	## state transfers on spawn without ongoing delta replication.
	## [br][br]
	## [b]How spawn discovery works:[/b]
	## [br]- [method SynchronizersCache.get_client_synchronizers] finds all
	##   [MultiplayerSynchronizer] nodes whose root_path points to the player.
	## [br]- Each synchronizer's replication_config properties are added as
	##   spawn-only.
	## [br]- [SaveComponent] pivots its root_path to [code]"."[/code] after
	##   baking, but spawn config paths were already resolved.
	func config_spawn_properties(target_node: Node) -> void:
		Netw.dbg.trace(
			"Configuring spawn properties for %s", [target_node.name]
		)
		replication_config = SceneReplicationConfig.new()

		if target_node.owner:
			var comp_path := target_node.owner.get_path_to(target_node)
			var uname_path := NodePath(str(comp_path) + ":username")
			_add_spawn_property(uname_path)

			var tp := target_node.owner.get_node_or_null("%TPComponent")
			if tp:
				var tp_path := target_node.owner.get_path_to(tp)
				var scene_path := NodePath(
					str(tp_path) + ":current_scene_path"
				)
				_add_spawn_property(scene_path)

		var syncs := SynchronizersCache.get_client_synchronizers(
			target_node.owner
			if target_node is SpawnerComponent else target_node
		)

		var sync_names := syncs.map(func(s): return s.name)
		Netw.dbg.debug(
			"Found %d synchronizers for spawn: [%s]",
			[syncs.size(), ", ".join(sync_names)]
		)

		for sync: MultiplayerSynchronizer in syncs:
			if sync == self or not sync.replication_config:
				continue

			var source: SceneReplicationConfig = sync.replication_config
			Netw.dbg.trace(
				"Adding %d properties from %s",
				[source.get_properties().size(), sync.name]
			)

			for property: NodePath in source.get_properties():
				if replication_config.has_property(property):
					continue
				_add_spawn_property(property)

	func _add_spawn_property(property: NodePath) -> void:
		replication_config.add_property(property)
		replication_config.property_set_replication_mode(
			property, SceneReplicationConfig.REPLICATION_MODE_NEVER
		)
		replication_config.property_set_spawn(property, true)
		replication_config.property_set_sync(property, false)
		replication_config.property_set_watch(property, false)


## Returns the [SpawnerComponent] with unique name [code]%SpawnerComponent[/code]
## from [param node], or [code]null[/code].
static func unwrap(node: Node) -> SpawnerComponent:
	return node.get_node_or_null("%SpawnerComponent")


func _init() -> void:
	name = "SpawnerComponent"
	unique_name_in_owner = true
	player_joined.connect(_on_player_joined)


func _ready() -> void:
	_dbg.trace("_ready for %s", [owner.name])

	if Engine.is_editor_hint():
		_validate_editor()
		return

	if is_multiplayer_authority():
		var mt := MultiplayerTree.resolve(self)
		if mt:
			mt.authority_client = self

	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)

	if username.is_empty():
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

	multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	var tp_layer := get_tp_layer()
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		_dbg.info(
			"Local player %s ready. Playing teleport transition.", [username]
		)
		tp_layer.teleport_in()


func _validate_editor() -> void:
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(
			_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST
		)


func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return

	if owner.get_multiplayer_authority() != 1:
		return

	_dbg.trace("Spawner `%s` entering tree.", [owner.name])
	assert(owner.name != "|")

	var authority := MultiplayerClientData.parse_authority(owner.name)
	if authority != 0 and authority_mode == AuthorityMode.CLIENT:
		_dbg.debug(
			"Setting authority for %s to %d", [owner.name, authority]
		)
		owner.set_multiplayer_authority(authority)

	_setup_spawn_sync(spawn_sync)


func _setup_spawn_sync(spawn: SpawnSynchronizer) -> void:
	spawn.config_spawn_properties(self)
	spawn.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _on_player_joined(client_data: MultiplayerClientData) -> void:
	var ctx := get_context()
	if not ctx:
		return
	var slot: MultiplayerTree.SpawnSlot = ctx.tree.get_spawn_slot(client_data.spawner_path)
	if not slot.is_valid():
		_dbg.error(
			"Player join failed: no active world or scene for scene '%s'.",
			[client_data.spawner_path.get_scene_name()],
			func(m): push_error(m)
		)
		return

	var sp_ctx := SpawnContext.new(self, slot, get_context())
	if spawn_function.is_valid():
		spawn_function.call(sp_ctx, client_data)
	else:
		sp_ctx.spawn_player(client_data)


func _on_peer_disconnected(peer_id: int) -> void:
	if (multiplayer and multiplayer.is_server()
			and get_multiplayer_authority() == peer_id):
		_dbg.info(
			"Peer %d disconnected. Freeing owned player %s.",
			[peer_id, owner.name]
		)
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return

	if is_multiplayer_authority():
		var mt := MultiplayerTree.resolve(self)
		if mt and mt.authority_client == self:
			mt.authority_client = null
