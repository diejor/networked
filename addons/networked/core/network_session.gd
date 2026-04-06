@tool
class_name NetworkSession
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


## When false, connect_player() will not promote this node to the scene root and
## will not auto-start a local server for localhost URLs. Set to false when
## NetworkSession is embedded as a child node (e.g. in test harnesses).
@export var manage_scene: bool = true

@export_group("Debug")
@export var init_client_data: MultiplayerClientData
@export_group("", "")

var server: MultiplayerTree

func connect_player(client_data: MultiplayerClientData) -> void:
	NetLog.trace("NetworkSession: connect_player called.")
	assert(client_data)
	assert(client_data.username)
	assert(client_data.spawner_path)
	
	await disconnect_player()

	if manage_scene:
		await _validate_current_scene()

	var url := client_data.url
	NetLog.info("Connecting player %s to %s" % [client_data.username, url])
	if manage_scene and _is_singleplayer(url):
		url = await _host_server()
	elif OS.has_feature("web"):
		if url.begins_with("ws"):
			client.backend = WebSocketBackend.new()
	
	var client_err: Error = await client.join(url, client_data.username)
	if client_err != OK:
		NetLog.error("Failed to join: %s" % error_string(client_err))
		return
	
	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)

## Hosts this NetworkSession as a dedicated server. Use this instead of
## connect_player() when this instance should be the server in an
## N-NetworkSession setup (e.g. test harnesses). The embedded client tree
## is reconfigured as the server in-place; no separate server node is created.
func host() -> Error:
	NetLog.trace("NetworkSession: host called.")
	client.is_server = true
	var err: Error = client.host()
	if err != OK:
		NetLog.error("Failed to host server: %s" % error_string(err))
	else:
		NetLog.info("Server hosted successfully.")
	return err


## Returns the address that clients should connect to after host() succeeds.
func get_host_address() -> String:
	return _resolve_server_address()


func disconnect_player() -> void:
	if not client.is_online():
		return
	
	NetLog.trace("NetworkSession: disconnect_player called.")
	NetLog.info("Disconnecting player.")
	SaveComponent.save_all_in(client.get_peer_context(client.multiplayer_api.get_unique_id()))
	client.multiplayer_peer.close()
	
	var timer := get_tree().create_timer(3.0)
	if await Async.timeout(client.multiplayer_api.server_disconnected, timer):
		NetLog.error("Couldn't disconnect from server.")

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
	NetLog.info("Closing embedded server.")
	if server:
		server.get_parent().remove_child(server)
		server.queue_free()
		server = null

func is_webrtc() -> bool:
	var script: Script = client.backend.get_script()
	var n := script.get_global_name().to_lower()
	var is_rtc := "rtc" in n or "tube" in n
	NetLog.debug("Backend check: class=%s is_webrtc=%s" % [script.get_global_name(), is_rtc])
	return is_rtc

func _host_server() -> String:
	NetLog.trace("NetworkSession: _host_server called.")
	if OS.has_feature("web") and not is_webrtc():
		client.backend = LocalLoopbackBackend.new()
	
	server = client.duplicate()
	server.is_server = true
	server.name = "Server"
	add_child(server)
	
	NetLog.info("Starting embedded server...")
	var server_err := server.host()
	var in_use := (server_err == ERR_ALREADY_IN_USE or server_err == ERR_CANT_CREATE)
	
	assert(server_err == OK or in_use, "Dedicated server failed to start: %s" % error_string(server_err))
	
	if in_use:
		NetLog.info("Server address already in use, using localhost.")
		server.queue_free.call_deferred()
		return "localhost"
		
	var addr := _resolve_server_address()
	NetLog.info("Embedded server started at: %s" % addr)
	return addr

func _resolve_server_address() -> String:
	var tree := server if server else client
	if not tree or not tree.backend:
		return "localhost"
	return tree.backend.get_join_address()

func _validate_current_scene() -> void:
	if not _is_current_scene():
		var tree := get_tree()
		owner.remove_child.call_deferred(self)
		tree.change_scene_to_node.call_deferred(self)
		await tree.scene_changed

func _is_singleplayer(url: String) -> bool:
	return url.is_empty() or "localhost" in url or "127.0.0.1" in url
