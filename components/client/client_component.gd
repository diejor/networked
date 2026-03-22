@tool
class_name ClientComponent
extends Node
## Manages client-specific multiplayer data, spawning logic, and player instantiation.
##
## Acts as the bridge between connecting peers and their physical representation in the game world,
## ensuring proper authority and spawn synchronization.

signal player_joined(client_data: MultiplayerClientData)
signal client_synchronized

@export_custom(PROPERTY_HINT_NONE, "replicated:never")
var username: String = "":
	set(user):
		username = user
var spawn_sync: MultiplayerSynchronizer:
	get:
		return %SpawnSynchronizer


## Helper function to easily retrieve the ClientComponent from a given node.
static func unwrap(node: Node) -> ClientComponent:
	return node.get_node_or_null("%ClientComponent")


func _init() -> void:
	unique_name_in_owner = true


func _ready() -> void:
	if EditorTooling.validate_and_halt(self, _validate_editor):
		return

	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)

	if username.is_empty():
		if not multiplayer.is_server():
			# Remove spawners from clients
			owner.queue_free()

		# Spawners only sync with the server
		SynchronizersCache.sync_only_server(owner)

	assert(
		owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly." % [owner.name, _on_owner_tree_entered],
	)

	multiplayer.peer_disconnected.connect(_on_peer_disconnected)

	var tp_layer := NetworkedAPI.get_tp_layer(self)
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		tp_layer.teleport_in()


func _get_configuration_warnings() -> PackedStringArray:
	return ReplicationValidator.get_configuration_warnings(self)


## Generates a new replication config for the spawn synchronizer by aggregating properties
## from all other client synchronizers attached to the target node.
func config_spawn_properties(target_node: Node) -> SceneReplicationConfig:
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

	if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
		owner.tree_entered.connect(_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST)


func _on_owner_tree_entered() -> void:
	if Engine.is_editor_hint():
		return

	assert(owner.name != "|")

	var name_authority: PackedStringArray = owner.name.split("|")
	if name_authority.size() == 2:
		var authority: int = name_authority[1].to_int()
		assert(authority != 0)
		owner.set_multiplayer_authority(authority)

	spawn_sync.root_path = spawn_sync.get_path_to(owner)
	spawn_sync.replication_config = config_spawn_properties(self)
	spawn_sync.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)


func _on_player_joined(client_data: MultiplayerClientData) -> void:
	assert(client_data.peer_id)
	assert(client_data.spawner_path)
	assert(client_data.username)

	var player_scene: PackedScene = load(owner.scene_file_path)
	var player: Node2D = player_scene.instantiate()
	var peer_id: int = client_data.peer_id
	var client: ClientComponent = player.get_node("%ClientComponent")

	client.username = client_data.username
	player.name = client.username + "|" + str(peer_id)

	var save_component: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.spawn(owner)

	client.username = client_data.username

	var lobby_manager := NetworkedAPI.get_lobby_manager(self)
	var tp_component: TPComponent = player.get_node_or_null("%TPComponent")

	if tp_component and save_component and lobby_manager:
		tp_component.spawn(lobby_manager)
	elif lobby_manager:
		# Safely fallback to fetching the lobby via .get() to prevent dictionary crashes
		var scene_name := client_data.spawner_path.get_scene_name()
		var lobby: Lobby = lobby_manager.active_lobbies.get(scene_name)

		if lobby:
			lobby.synchronizer.track_player(player)
			lobby.level.add_child(player)
			player.owner = lobby.level
		else:
			push_error("ClientComponent: Could not find active lobby for scene %s" % scene_name)


func _on_connect_player(client_data: MultiplayerClientData) -> void:
	assert(get_tree().current_scene is MultiplayerNetwork)
	var network: MultiplayerNetwork = get_tree().current_scene
	network.connect_player(client_data)


func _on_peer_disconnected(peer_id: int) -> void:
	if multiplayer and multiplayer.is_server() and get_multiplayer_authority() == peer_id:
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()
