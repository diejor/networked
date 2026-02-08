class_name Lobby
extends Node

@export var scene_sync: SceneSynchronizer
@export var scene_spawner: MultiplayerSpawner

var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		level.owner = self
