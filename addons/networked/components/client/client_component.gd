@tool
class_name ClientComponent
extends NetComponent
## The authoritative bridge between a connecting peer and their in-world 
## representation.
##
## Add this node to your player scene. The component handles multiplayer 
## authority setup, and player teardown on disconnect.
## 
## [codeblock]
## # Retrieve from any node in the player scene:
## var client := ClientComponent.unwrap(player_node)
## if client:
##     print(client.username)
## [/codeblock]

## Emitted on the server when a peer requests to join.
signal player_joined(client_data: MultiplayerClientData)
## Emitted each time a client-owned [MultiplayerSynchronizer] delivers a delta update.
signal client_synchronized

enum AuthorityMode { 
	## The node's authority is set to the player's peer ID (default, use for
	## client-authoritative movement)
	CLIENT, 
	## The node stays at authority 1. Use when the server drives all simulation.
	SERVER_AUTHORITATIVE 
}

## Controls how multiplayer authority is assigned to the owning player node on spawn.
@export var authority_mode: AuthorityMode = AuthorityMode.CLIENT

## The username of the player associated with this component, assigned by the player.
var username: String = ""

var _dbg: NetwHandle = Netw.dbg.handle(self)


## The [MultiplayerSynchronizer] used for initial spawn state replication.
var spawn_sync: SpawnSynchronizer:
	get:
		if not spawn_sync:
			# TODO: not happy of setting this here but we need the synchronizer as soon
			# as possible to prevent node not found errors.
			spawn_sync = SpawnSynchronizer.new(self)
		return %SpawnSynchronizer

## Allows the server to control the visibility of client-authoritative players.
class SpawnSynchronizer extends MultiplayerSynchronizer:
	func _init(client: ClientComponent) -> void:
		name = "SpawnSynchronizer"
		unique_name_in_owner = true
		visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
		client.add_child(self)
		owner = client
		root_path = get_path_to(client.owner)
	
	## Builds a [SceneReplicationConfig] collecting properties from all client 
	## synchronizers of [param target_node].
	##
	## Marks each collected property as spawn-only 
	## ([code]REPLICATION_MODE_NEVER[/code] with spawn enabled)
	## so initial state is transferred on spawn without ongoing delta replication.
	func config_spawn_properties(target_node: Node) -> void:
		Netw.dbg.trace("Configuring spawn properties for %s" % target_node.name)
		
		replication_config = SceneReplicationConfig.new()
		
		# Explicitly add username from the ClientComponent itself.
		# Path must be relative to the root_path (the player node).
		if target_node.owner:
			var component_path := target_node.owner.get_path_to(target_node)
			var username_path := NodePath(str(component_path) + ":username")
			_add_spawn_property(username_path)
			
			var tp := target_node.owner.get_node_or_null("%TPComponent")
			if tp:
				var tp_path := target_node.owner.get_path_to(tp)
				var scene_path := NodePath(str(tp_path) + ":current_scene_path")
				_add_spawn_property(scene_path)
		
		var syncs := SynchronizersCache.get_client_synchronizers(target_node.owner 
			if target_node is ClientComponent else target_node)
		
		for sync: MultiplayerSynchronizer in syncs:
			if sync == self or not sync.replication_config:
				continue
			
			var source_config: SceneReplicationConfig = sync.replication_config
			for property: NodePath in source_config.get_properties():
				if replication_config.has_property(property):
					continue
				
				_add_spawn_property(property)
	
	func _add_spawn_property(property: NodePath) -> void:
		replication_config.add_property(property)
		replication_config.property_set_replication_mode(property, 
			SceneReplicationConfig.REPLICATION_MODE_NEVER)
		replication_config.property_set_spawn(property, true)
		replication_config.property_set_sync(property, false)
		replication_config.property_set_watch(property, false)


## Returns the [ClientComponent] with unique name [code]%ClientComponent[/code] 
## from [param node], or [code]null[/code].
static func unwrap(node: Node) -> ClientComponent:
	return node.get_node_or_null("%ClientComponent")


## Parses the multiplayer authority from a node name formatted as 
## [code]username|peer_id[/code].
## Returns [param peer_id] as an [int], or  [code]0[/code] 
##if the name does not contain the separator.
static func parse_authority(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0


func _init() -> void:
	## TODO: move name conventions to NetComponent
	name = "ClientComponent"
	unique_name_in_owner = true
	
	player_joined.connect(_on_player_joined)


func _ready() -> void:
	_dbg.trace("_ready for %s" % owner.name)
	
	if Engine.is_editor_hint():
		_validate_editor()
		return
	
	# TODO: move client_synchronized signal to NetComponent
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)

	if username.is_empty():
		if not multiplayer.is_server():
			_dbg.trace("Freeing spawner node `%s` because we are in client." % owner.name)
			owner.queue_free()

		SynchronizersCache.sync_only_server(owner)

	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly. Reload the player scene to connect \
automatically." % [owner.name, _on_owner_tree_entered])

	multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	var tp_layer := get_tp_layer()
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		_dbg.info("Local player %s ready. Playing teleport transition." % username)
		tp_layer.teleport_in()


func _validate_editor() -> void:
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST)


func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return
		
	# Guard against double calls if the signal is still connected in existing scenes.
	if owner.get_multiplayer_authority() != 1:
		return

	_dbg.trace("Client `%s` entering tree." % [owner.name])
	assert(owner.name != "|")

	var authority := parse_authority(owner.name)
	if authority != 0 and authority_mode == AuthorityMode.CLIENT:
		_dbg.debug("Setting authority for %s to %d" % [owner.name, authority])
		owner.set_multiplayer_authority(authority)
	# SERVER_AUTHORITATIVE: node stays at peer 1; peer_id in name is for routing only.

	_setup_spawn_sync(spawn_sync)

func _setup_spawn_sync(spawn: SpawnSynchronizer) -> void:
	spawn.config_spawn_properties(self)
	spawn.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _get_player_scene() -> PackedScene:
	return load(owner.scene_file_path)


func _instantiate_player(client_data: MultiplayerClientData) -> Node:
	_dbg.trace("Instantiating player for %s (ID: %d)" % [client_data.username, client_data.peer_id])
	var player := _get_player_scene().instantiate()
	var client: ClientComponent = player.get_node("%ClientComponent")
	client.username = client_data.username
	player.name = "%s|%s" % [client_data.username, client_data.peer_id]
	return player


func _on_player_joined(client_data: MultiplayerClientData) -> void:
	var ctx := get_session().get_spawn_context(client_data.spawner_path)
	if not ctx.is_valid():
		_dbg.error("Player join failed: no active world or lobby for scene '%s'." \
			% client_data.spawner_path.get_scene_name(), func(m): push_error(m))
		return

	var span: NetSpan = _dbg.span("player_join", {
		"username": client_data.username,
		"peer_id": client_data.peer_id,
		"authority_mode": authority_mode,
	})
	span.step("joined")
	_dbg.info("Player joined: %s (ID: %d)" % [client_data.username, client_data.peer_id])

	if not client_data.peer_id or not client_data.spawner_path or client_data.username.is_empty():
		_dbg.error("Player join failed: invalid client data.", func(m): push_error(m))
		span.end()
		return

	var player := _instantiate_player(client_data)

	var save_component: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save_component:
		_dbg.debug("Loading player with SaveComponent for player `%s`." % client_data.username)
		save_component.spawn(owner, span)

	var tp_component: TPComponent = player.get_node_or_null("%TPComponent")

	if tp_component and save_component and ctx.has_lobby():
		_dbg.debug("Using TPComponent to spawn player `%s`." % client_data.username)
		tp_component.spawn(get_session().get_lobby_manager())
	else:
		ctx.place_player(player)

	span.end()


func _on_connect_player(client_data: MultiplayerClientData) -> void:
	_dbg.trace("Connecting player %s" % client_data.username)
	var network := get_tree().current_scene as NetworkSession
	if not network:
		_dbg.error("Could not connect player: current scene is not a NetworkSession.", func(m): push_error(m))
		return
	network.connect_player(client_data)


func _on_peer_disconnected(peer_id: int) -> void:
	if (multiplayer and multiplayer.is_server()
		and get_multiplayer_authority() == peer_id):
		_dbg.info("Peer %d disconnected. Freeing owned player %s." % [peer_id, owner.name])
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()
