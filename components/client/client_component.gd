class_name ClientComponent
extends NodeComponent

@warning_ignore("unused_signal")
signal player_joined(client_data: Dictionary)

@export_custom(PROPERTY_HINT_NONE, "replicated") 
var username: String = "":
	set(user):
		username = user
		username_label.text = user

var username_label: RichTextLabel:
	get: return %ClientHUD/%UsernameLabel

func _ready() -> void:
	super._ready()
	if "Spawner" in owner.name:
		if not multiplayer.is_server():
			owner.queue_free()
	
	assert(owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly." % [owner.name, _on_owner_tree_entered])


func _on_owner_tree_entered() -> void:
	assert(owner.name != "|")
	var name_authority: PackedStringArray = owner.name.split("|")
	if name_authority.size() == 2:
		var authority: int = name_authority[1].to_int()
		assert(authority != 0)
		owner.set_multiplayer_authority(authority)


func _on_player_joined(client_data: Dictionary) -> void:
	assert(client_data.peer_id)
	assert(client_data.scene_path)
	assert(client_data.username)
	
	if client_data.scene_path != owner.scene_file_path:
		return
	
	var player_scene: PackedScene = load(client_data.scene_path as String)
	var player: Node2D = player_scene.instantiate()
	
	var peer_id: int = client_data.peer_id
	var client: ClientComponent = player.get_node("%ClientComponent")
	client.username = client_data.username
	player.name = client.username + "|" + str(peer_id)
	
	@warning_ignore("untyped_declaration")
	var save_component = player.get_node_or_null("%SaveComponent")
	if save_component:
		@warning_ignore("unsafe_method_access")
		save_component.spawn(owner)

	# TODO: SaveComponent might override some values from a spawner, that are tracked
	# but we dont really want to override.
	client.username = client_data.username
	
	
	var lobby: Lobby = owner.owner.owner
	@warning_ignore("untyped_declaration")
	var tp_component = player.get_node_or_null("%TPComponent")
	if tp_component:
		@warning_ignore("unsafe_method_access")
		tp_component.spawn(lobby_manager)
	else:
		lobby.scene_sync.track_player(player)
		lobby.level.add_child(player)
	
