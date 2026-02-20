class_name MultiplayerNetwork
extends Node


@export var client: MultiplayerTree

### 
@export var init_client_data: MultiplayerClientData

func _ready() -> void:
	get_tree().scene_changed.connect(ensure_configured)
		
	if owner:
		owner.remove_child.call_deferred(self)
	
	if init_client_data:
		configure(init_client_data)
		if owner:
			get_tree().change_scene_to_node.call_deferred(self)

func ensure_configured() -> void:
	assert(get_tree().scene_changed.is_connected(connect_player), "`%s` \
should be called before changing to `%s`." % [configure.get_method(), name])


func configure(client_data: MultiplayerClientData) -> void:
	validate_web()
	
	var scene_tree := Engine.get_main_loop() as SceneTree
	if owner:
		scene_tree.scene_changed.connect(connect_player.bind(client_data))
	else:
		connect_player(client_data)


func validate_web() -> void:
	if OS.has_feature("web"):
		client.backend = LocalLoopbackBackend.new()


func host_server() -> void:
	var server: MultiplayerTree = client.duplicate()
	server.is_server = true
	server.name = "Server"
	add_child(server)
	
	var server_err := server.host()
	var in_use := server_err == ERR_ALREADY_IN_USE or server_err == ERR_CANT_CREATE
	assert(server_err == OK or in_use,
		"Dedicated server failed to start: %s" % error_string(server_err))
	if in_use:
		server.queue_free.call_deferred()


func connect_player(client_data: MultiplayerClientData) -> void:
	assert(client_data)
	assert(client_data.username)
	assert(client_data.scene_path)

	if client_data.url.is_empty():
		host_server()
		
	var client_err: Error = await client.join("localhost", client_data.username)
	if client_err != OK:
		push_warning("Failed: %s" % error_string(client_err))
		return
	
	client_data.peer_id = client.uid
	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)
