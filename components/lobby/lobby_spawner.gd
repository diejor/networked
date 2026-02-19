class_name MultiplayerLobbySpawner
extends MultiplayerSpawner

var clients: Array[String]

func _enter_tree() -> void:
	clear_spawnable_scenes()
	assert(not clients.is_empty())
	
	for client in clients:
		add_spawnable_scene(client)
