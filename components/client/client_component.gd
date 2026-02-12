class_name ClientComponent
extends NodeComponent

@warning_ignore("unused_signal")
signal player_joined(client_data: ClientData)

@export_custom(PROPERTY_HINT_NONE, "replicated") 
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
	if username.is_empty() and not multiplayer.is_server():
		owner.queue_free()
	
	assert(owner.tree_entered.is_connected(_on_owner_tree_entered),
		"Signal `tree_entered` of `%s` must be connected to `%s`, otherwise, \
the authority will not be set correctly." % [owner.name, _on_owner_tree_entered])

	api.peer_disconnected.connect(_on_peer_disconnected)
	
	if is_multiplayer_authority() and not multiplayer.is_server():
		transition_player.teleport_in_animation()

func _on_owner_tree_entered() -> void:
	assert(owner.name != "|")
	var name_authority: PackedStringArray = owner.name.split("|")
	if name_authority.size() == 2:
		var authority: int = name_authority[1].to_int()
		assert(authority != 0)
		owner.set_multiplayer_authority(authority)


func _on_player_joined(client_data: ClientData) -> void:
	assert(client_data.peer_id)
	assert(client_data.scene_path)
	assert(client_data.username)
	
	if (not (ResourceUID.ensure_path(client_data.scene_path) 
		== ResourceUID.ensure_path(owner.scene_file_path)
		and get_multiplayer_authority() == MultiplayerPeer.TARGET_PEER_SERVER)):
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

	# TODO: SaveComponent might override some values from a spawner, that are tracked
	# but we dont really want to override.
	client.username = client_data.username
	
	var tp_component: TPComponent = player.get_node_or_null("%TPComponent")
	if tp_component:
		tp_component.spawn(lobby_manager)
	else:
		lobby.scene_sync.track_player(player)
		lobby.level.add_child(player)
		player.owner = lobby.level


func _on_peer_disconnected(peer_id: int) -> void:
	if multiplayer.is_server() and get_multiplayer_authority() == peer_id:
		state_sync.only_server()
		
		owner.set_multiplayer_authority(MultiplayerPeer.TARGET_PEER_SERVER)
		owner.queue_free.call_deferred()
