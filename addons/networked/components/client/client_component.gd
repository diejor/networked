@tool
class_name ClientComponent
extends NetComponent
## Component that acts as the authoritative bridge between a connecting peer and their in-world representation.
##
## Add this node (with unique name [code]%ClientComponent[/code]) to your player scene.
## The component handles multiplayer authority setup, spawn-property aggregation, and player
## teardown on disconnect. Connect [signal player_joined] to [method _on_player_joined] to
## trigger player instantiation.
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

## Controls how multiplayer authority is assigned to the owning player node on spawn.
enum AuthorityMode { 
	## The node's authority is set to the player's peer ID (default, use for
	## client-authoritative movement)
	CLIENT, 
	## The node stays at authority 1; Use when the server drives all simulation.
	SERVER_AUTHORITATIVE 
}

@export var authority_mode: AuthorityMode = AuthorityMode.CLIENT

## The username of the player associated with this component, assigned by the player.
@export_custom(PROPERTY_HINT_NONE, "replicated:never")
var username: String = "":
	set(user):
		username = user
## The [MultiplayerSynchronizer] used for initial spawn state replication.
var spawn_sync: SpawnSynchronizer:
	get:
		if not spawn_sync:
			spawn_sync = SpawnSynchronizer.new()
			add_child(spawn_sync)
			spawn_sync.owner = self
		return spawn_sync


## Returns the [ClientComponent] with unique name [code]%ClientComponent[/code] from [param node], or [code]null[/code].
static func unwrap(node: Node) -> ClientComponent:
	return node.get_node_or_null("%ClientComponent")


## Parses the multiplayer authority from a node name formatted as "username|peer_id".
## Returns the peer_id as an int, or 0 if the name does not contain the separator.
static func parse_authority(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0


func _init() -> void:
	unique_name_in_owner = true
	player_joined.connect(_on_player_joined)

func _ready() -> void:
	log_trace("ClientComponent: _ready for %s" % owner.name)
	
	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST)
	
	if EditorTooling.validate_and_halt(self, _validate_editor):
		return
	
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)

	if username.is_empty():
		if not multiplayer.is_server():
			log_trace("ClientComponent: username is empty for %s during _ready, freeing." % owner.name)
			owner.queue_free()

		SynchronizersCache.sync_only_server(owner)

	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly." % [owner.name, _on_owner_tree_entered],
	)

	multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	var tp_layer := get_tp_layer()
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		log_info("Local player %s ready. Playing teleport transition." % username)
		tp_layer.teleport_in()


func _get_configuration_warnings() -> PackedStringArray:
	return ReplicationValidator.get_configuration_warnings(self)


## Builds a [SceneReplicationConfig] for the spawn synchronizer by collecting properties from all client synchronizers.
##
## Marks each collected property as spawn-only ([code]REPLICATION_MODE_NEVER[/code] with spawn enabled)
## so initial state is transferred on spawn without ongoing delta replication.
func config_spawn_properties(target_node: Node) -> SceneReplicationConfig:
	log_trace("ClientComponent: Configuring spawn properties for %s" % target_node.name)
	var new_config := SceneReplicationConfig.new()
	var syncs := SynchronizersCache.get_client_synchronizers(target_node.owner if target_node is ClientComponent else target_node)

	for sync: MultiplayerSynchronizer in syncs:
		if sync == spawn_sync or not sync.replication_config:
			continue

		var source_config: SceneReplicationConfig = sync.replication_config
		for property: NodePath in source_config.get_properties():
			if not new_config.has_property(property):
				new_config.add_property(property)
				new_config.property_set_replication_mode(property, SceneReplicationConfig.REPLICATION_MODE_NEVER)
				new_config.property_set_spawn(property, true)
				new_config.property_set_sync(property, false)
				new_config.property_set_watch(property, false)

	return new_config


func _validate_editor() -> void:
	ReplicationValidator.verify_and_configure(self)


func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return

	assert(owner.name != "|")

	var authority := parse_authority(owner.name)
	if authority != 0 and authority_mode == AuthorityMode.CLIENT:
		log_debug("Setting authority for %s to %d" % [owner.name, authority])
		owner.set_multiplayer_authority(authority)
	# SERVER_AUTHORITATIVE: node stays at peer 1; peer_id in name is for routing only.

	_setup_spawn_sync(spawn_sync)

func _setup_spawn_sync(spawn: SpawnSynchronizer) -> void:
	spawn.root_path = spawn.get_path_to(owner)
	spawn.replication_config = config_spawn_properties(self)
	spawn.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _get_player_scene() -> PackedScene:
	return load(owner.scene_file_path)


func _instantiate_player(client_data: MultiplayerClientData) -> Node:
	log_trace("ClientComponent: Instantiating player for %s (ID: %d)" % [client_data.username, client_data.peer_id])
	var player := _get_player_scene().instantiate()
	var client: ClientComponent = player.get_node("%ClientComponent")
	client.username = client_data.username
	player.name = "%s|%s" % [client_data.username, client_data.peer_id]
	return player


func _place_in_lobby(player: Node, lobby: Lobby) -> void:
	log_info("Placing player %s into lobby %s" % [player.name, lobby.name])
	lobby.add_player(player)


func _on_player_joined(client_data: MultiplayerClientData) -> void:
	log_info("Player joined: %s (ID: %d)" % [client_data.username, client_data.peer_id])
	assert(client_data.peer_id)
	assert(client_data.spawner_path)
	assert(client_data.username)

	var player := _instantiate_player(client_data)

	var save_component: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save_component:
		log_debug("Spawning SaveComponent for player %s" % client_data.username)
		save_component.spawn(owner)

	var lobby_manager := get_lobby_manager()
	var tp_component: TPComponent = player.get_node_or_null("%TPComponent")

	if tp_component and save_component and lobby_manager:
		log_debug("Spawning TPComponent for player %s" % client_data.username)
		tp_component.spawn(lobby_manager)
	elif lobby_manager:
		var scene_name := client_data.spawner_path.get_scene_name()
		var lobby: Lobby = lobby_manager.active_lobbies.get(scene_name)

		if lobby:
			_place_in_lobby(player, lobby)
		else:
			log_error("ClientComponent: Could not find active lobby for scene %s" % scene_name)


func _on_connect_player(client_data: MultiplayerClientData) -> void:
	log_trace("ClientComponent: Connecting player %s" % client_data.username)
	assert(get_tree().current_scene is NetworkSession)
	var network: NetworkSession = get_tree().current_scene
	network.connect_player(client_data)


func _on_peer_disconnected(peer_id: int) -> void:
	if multiplayer and multiplayer.is_server() and get_multiplayer_authority() == peer_id:
		log_info("Peer %d disconnected. Freeing owned player %s." % [peer_id, owner.name])      
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()
