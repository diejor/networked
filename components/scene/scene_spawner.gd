class_name SceneSpawner
extends MultiplayerSpawner

func _init() -> void:
	clear_spawnable_scenes()
	
	for client in Networked.config.clients:
		add_spawnable_scene(client)
