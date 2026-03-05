@tool
class_name MultiplayerNetwork
extends Node

## Manages the active multiplayer session and network state.
##
## This node handles hosting, joining, and disconnecting from server sessions.
## To start networking, you should exclusively use the [method connect_player] function.


## The primary network manager for the local player.
##
## You must assign a [MultiplayerTree] node to this property in the inspector for the network to function. 
## It is responsible for joining external servers and sending/receiving RPCs.
## [br][br]
## [b]Important:[/b] If the game is running as a dedicated server (headless mode), this node is automatically 
## destroyed to save resources, and the network operates entirely through the [member server] variable instead.
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
## Optional data used to automatically connect when the scene loads.
##
## If you assign a [MultiplayerClientData] resource here in the inspector, 
## the game will automatically attempt to host or join using this data as 
## soon as the node is ready. 
## [br][br]
## [b]Note:[/b] This is strictly for testing and debugging. Leave this empty 
## in your final production builds.
@export var init_client_data: MultiplayerClientData
@export_group("", "")


## The dedicated server instance, created dynamically when hosting.
##
## [br][br]
## [b]Note:[/b] This variable will remain [code]null[/code] if the instance is strictly running as a connected 
## client. You should never assign this manually.
var server: MultiplayerTree


## Initiates a multiplayer connection or hosts a local singleplayer session.
##
## This is the primary entry point for using the `MultiplayerNetwork`. Provide it with 
## a valid [MultiplayerClientData] object containing the target URL, username, and scene path.
## If the URL is empty or points to localhost, a server will be hosted automatically.
func connect_player(client_data: MultiplayerClientData) -> void:
	assert(client_data)
	assert(client_data.username)
	assert(client_data.scene_path)
	
	await disconnect_player()
	await _validate_current_scene()
	
	var url := client_data.url
	if _is_singleplayer(url):
		url = "localhost"
		_host_server()
	elif OS.has_feature("web"):
		client.backend = WebSocketBackend.new()
	
	var client_err: Error = await client.join(url, client_data.username)
	if client_err != OK:
		push_warning("Failed: %s" % error_string(client_err))
		return
	
	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data.serialize()
	)


## Saves the game state and safely closes the connection to the active multiplayer server.
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
		push_warning("Connecting with debug `init_client_data`.")
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


func _host_server() -> void:
	if OS.has_feature("web"):
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


func _validate_current_scene() -> void:
	if not _is_current_scene():
		var tree := get_tree()
		owner.remove_child(self)
		tree.change_scene_to_node.call_deferred(self)
		await tree.scene_changed


func _is_singleplayer(url: String) -> bool:
	return url.is_empty() or "localhost" in url or "127.0.0.1" in url
