class_name Network
extends Node

@export var client: GameClient
@export var server: GameServer

@export_file var player_scene: String

var username: String:
	get:
		if username.is_empty():
			var candidate := OS.get_environment("USERNAME")
			if candidate.is_empty():
				candidate = "player"
			username = candidate
		return username

func _ready() -> void:
	var server_err := server.init()
	assert(server_err == OK or server_err == ERR_ALREADY_IN_USE,
		"Dedicated server failed to start: %s" % error_string(server_err))
		
	var client_err: Error = await client.connect_client("localhost", username)
	if client_err != OK:
		push_warning("Failed: %s" % error_string(client_err))
		return
	
	var client_data: Dictionary = {
		username = username,
		scene_path = player_scene,
		peer_id = client.uid
	}
	
	client.scene_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER, 
		client_data
	)
