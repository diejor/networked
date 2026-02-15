class_name Lobby
extends Node

@export var synchronizer: MultiplayerLobbySynchronizer
@export var spawner: MultiplayerLobbySpawner

var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		level.owner = self
