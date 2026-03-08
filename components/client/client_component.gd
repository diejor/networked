@tool
class_name ClientComponent
extends NodeComponent

signal player_joined(client_data: MultiplayerClientData)

var spawn_sync: MultiplayerSynchronizer:
	get: return %SpawnSynchronizer

@export_custom(PROPERTY_HINT_NONE, "replicated:never") 
var username: String = "":
	set(user):
		username = user
		if username_label:
			username_label.text = user

var username_label: RichTextLabel:
	get:
		var label := get_node_or_null("%ClientHUD/%UsernameLabel")
		return label


func _init() -> void:
	unique_name_in_owner = true


func _ready() -> void:
	super._ready()
	if Engine.is_editor_hint():
		if owner and not owner.tree_entered.is_connected(_on_owner_tree_entered):
			owner.tree_entered.connect(_on_owner_tree_entered, ConnectFlags.CONNECT_PERSIST)
		return
	
	if username.is_empty():
		if not multiplayer.is_server():
			owner.queue_free()
		sync_only_server()
	
	assert(owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly." % [owner.name, _on_owner_tree_entered])

	api.peer_disconnected.connect(_on_peer_disconnected)
	
	if not multiplayer.is_server() and is_multiplayer_authority() and tp_layer:
		tp_layer.teleport_in()


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
	assert(client_data.scene_path)
	assert(client_data.username)
	
	var is_valid_scene: bool = ResourceUID.ensure_path(client_data.scene_path) == ResourceUID.ensure_path(owner.scene_file_path)
	if not is_valid_scene or get_multiplayer_authority() != MultiplayerPeer.TARGET_PEER_SERVER:
		return
	
	var player_scene: PackedScene = load(client_data.scene_path as String)
	var player: Node2D = player_scene.instantiate()
	var peer_id: int = client_data.peer_id
	var client: ClientComponent = player.get_node("%ClientComponent")
	
	client.username = client_data.username
	player.name = client.username + "|" + str(peer_id)
	
	var save_component: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.spawn(owner)

	client.username = client_data.username
	
	var tp_component: TPComponent = player.get_node_or_null("%TPComponent")
	if tp_component and save_component:
		tp_component.spawn(lobby_manager)
	else:
		lobby.synchronizer.track_player(player)
		lobby.level.add_child(player)
		player.owner = lobby.level


func _on_peer_disconnected(peer_id: int) -> void:
	if multiplayer and multiplayer.is_server() and get_multiplayer_authority() == peer_id:
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()


func config_spawn_properties(base: NodeComponent) -> SceneReplicationConfig:
	var new_config := SceneReplicationConfig.new()
	var syncs := base.get_client_synchronizers()
	
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


static func unwrap(node: Node) -> ClientComponent:
	return node.get_node_or_null("%ClientComponent")
