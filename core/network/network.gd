class_name MultiplayerNetwork
extends Node


@export var client: MultiplayerTree:
	set(peer):
		peer.multiplayer_api.server_disconnected.connect(close_server)
		client = peer


@export_group("Debug")
## Immediately connects the player with `init_client_data`.
@export var init_client_data: MultiplayerClientData
@export_group("", "")

var server: MultiplayerTree


func is_current_scene() -> bool:
	return get_tree().current_scene == self


func _ready() -> void:
	if init_client_data:
		push_warning("Connecting with debug `init_client_data`.")
		connect_player(init_client_data)
	
	if DisplayServer.get_name() == "headless":
		host_server()


func validate_web() -> void:
	if OS.has_feature("web"):
		client.backend = LocalLoopbackBackend.new()


func close_server() -> void:
	if server:
		remove_child(server)


func host_server() -> void:
	server = client.duplicate()
	server.is_server = true
	server.name = "Server"
	add_child(server)
	
	var server_err := server.host()
	var in_use := (server_err == ERR_ALREADY_IN_USE or server_err == ERR_CANT_CREATE)
	assert(server_err == OK or in_use,
		"Dedicated server failed to start: %s" % error_string(server_err))
	if in_use:
		server.queue_free.call_deferred()
	
	if DisplayServer.get_name() == "headless":
		client.queue_free()

## Validate the active (connected to a server) `MultiplayerNetwork` is only valid 
## when running as the `current_scene` of the `SceneTree`.
func validate_current_scene() -> void:
	if not is_current_scene():
		
		var tree := get_tree()
		owner.remove_child(self)
		tree.change_scene_to_node.call_deferred(self)
		await tree.scene_changed


func connect_player(client_data: MultiplayerClientData) -> void:
	assert(client_data)
	assert(client_data.username)
	assert(client_data.scene_path)
	
	
	await disconnect_player()
	validate_web()
	await validate_current_scene()
	
	
	var url := client_data.url
	if url.is_empty() or "localhost" in url or "127.0.0.1" in url:
		url = "localhost"
		host_server()
		
	var client_err: Error = await client.join(url, client_data.username)
	if client_err != OK:
		push_warning("Failed: %s" % error_string(client_err))
		return
	
	client_data.peer_id = client.uid
	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)


func disconnect_player() -> void:
	if client.is_online():
		SaveComponent.save_game()
		client.multiplayer_peer.close()
		var timer := get_tree().create_timer(3.0)
		if await Async.timeout(client.multiplayer_api.server_disconnected, timer):
			push_error("Couldn't disconnect from server.")
