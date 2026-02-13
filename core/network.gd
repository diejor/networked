class_name MultiplayerNetwork
extends Node

@export var client: MultiplayerClient
@export var server: MultiplayerServer

### 
@export var init_client_data: MultiplayerClientData

func _ready() -> void:
	get_tree().scene_changed.connect(ensure_configured)
	owner.remove_child.call_deferred(self)
	if init_client_data:
		var error_str = init_client_data.is_valid_scene()
		assert(error_str.is_empty(), error_str)
		configure(init_client_data)
		get_tree().change_scene_to_node.call_deferred(self)

func ensure_configured() -> void:
	assert(get_tree().scene_changed.is_connected(connect_player), "`%s` \
should be called before changing to `%s`." % [configure.get_method(), name])


func configure(client_data: MultiplayerClientData) -> void:
	var scene_tree := Engine.get_main_loop() as SceneTree
	scene_tree.scene_changed.connect(connect_player.bind(client_data))
	validate_web()

func validate_web() -> void:
	if OS.has_feature("web"):
		client.backend = WebRTCLoopbackClientBackend.new()
		server.backend = WebRTCLoopbackServerBackend.new()

func connect_player(client_data: MultiplayerClientData) -> void:
	assert(client_data)
	assert(client_data.username)
	assert(client_data.scene_path)

	var server_err := server.init()
	assert(server_err == OK or server_err == ERR_ALREADY_IN_USE,
		"Dedicated server failed to start: %s" % error_string(server_err))
	if server_err == ERR_ALREADY_IN_USE:
		server.queue_free.call_deferred()
		
	var client_err: Error = await client.connect_client("localhost", client_data.username)
	if client_err != OK:
		push_warning("Failed: %s" % error_string(client_err))
		return

	client_data.peer_id = client.uid
	client.scene_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)
