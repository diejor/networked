@tool
class_name MultiplayerNetwork
extends Node

@export var client: MultiplayerTree:
	set(peer):
		client = peer
		update_configuration_warnings()
		
		if Engine.is_editor_hint() or not client:
			return
			
		if not client.server_disconnected.is_connected(_close_server):
			client.server_disconnected.connect(_close_server)
		
		if DisplayServer.get_name() == "headless":
			client.queue_free()


@export_group("Debug")
@export var init_client_data: MultiplayerClientData
@export_group("", "")

var server: MultiplayerTree

func connect_player(client_data: MultiplayerClientData) -> void:
	assert(client_data)
	assert(client_data.username)
	assert(client_data.spawner_path)
	
	await disconnect_player()
	await _validate_current_scene()
	
	var url := client_data.url
	if _is_singleplayer(url):
		url = await _host_server()
	elif OS.has_feature("web"):
		if url.begins_with("ws"):
			client.backend = WebSocketBackend.new()
	
	var client_err: Error = await client.join(url, client_data.username)
	if client_err != OK:
		push_error("Failed: %s" % error_string(client_err))
		return
	
	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)

func disconnect_player() -> void:
	if not client.is_online():
		return

	SaveComponent.save_game()
	client.multiplayer_peer.close()
	
	var timer := get_tree().create_timer(3.0)
	if await Async.timeout(client.multiplayer_api.server_disconnected, timer):
		push_error("Couldn't disconnect from server.")

func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if not client:
		warnings.append("A MultiplayerTree must be assigned to the 'client' property for the network to function.")
	return warnings

func _ready() -> void:
	if Engine.is_editor_hint():
		return
		
	if init_client_data:
		connect_player(init_client_data)
	
	if DisplayServer.get_name() == "headless":
		_host_server()

func _is_current_scene() -> bool:
	return get_tree().current_scene == self

func _close_server() -> void:
	if server:
		server.get_parent().remove_child(server)
		server.queue_free()
		server = null

func is_webrtc() -> bool:
	var script: Script = client.backend.get_script()
	var n := script.get_global_name().to_lower()
	var is_rtc := "rtc" in n or "tube" in n
	print(script.get_global_name())
	return is_rtc

func _host_server() -> String:
	if OS.has_feature("web") and not is_webrtc():
		client.backend = LocalLoopbackBackend.new()
	
	server = client.duplicate()
	server.is_server = true
	server.name = "Server"
	add_child(server)
	
	var server_err := server.host()
	var in_use := (server_err == ERR_ALREADY_IN_USE or server_err == ERR_CANT_CREATE)
	
	assert(server_err == OK or in_use, "Dedicated server failed to start: %s" % error_string(server_err))
	
	if in_use:
		server.queue_free.call_deferred()
		return "localhost"
		
	return _resolve_server_address()

func _resolve_server_address() -> String:
	if not server or not server.backend:
		return "localhost"
		
	return server.backend.get_join_address()

func _validate_current_scene() -> void:
	if not _is_current_scene():
		var tree := get_tree()
		owner.remove_child(self)
		tree.change_scene_to_node.call_deferred(self)
		await tree.scene_changed

func _is_singleplayer(url: String) -> bool:
	return url.is_empty() or "localhost" in url or "127.0.0.1" in url
