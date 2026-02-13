class_name SceneSpawner
extends MultiplayerSpawner

var lobby_manager: MultiplayerLobbyManager:
	get: return get_parent().get_parent()

var network: MultiplayerNetwork:
	get: return lobby_manager.network

var clients: Array[String]:
	get: return network.config.clients

func _enter_tree() -> void:
	clear_spawnable_scenes()
	
	for client in clients:
		add_spawnable_scene(client)
