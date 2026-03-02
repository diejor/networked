class_name Lobby
extends Node

@export var synchronizer: MultiplayerLobbySynchronizer

var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self


func hook_spawn_signals(level: Node) -> void:
	var spawners := get_spawners(level)
	var is_direct_spawner := func(spawner: MultiplayerSpawner) -> bool:
		return spawner.get_path_to(level) == spawner.spawn_path
	spawners = spawners.filter(is_direct_spawner)
	for spawner in spawners:
		spawner.spawned.connect(synchronizer._on_spawned)
		spawner.despawned.connect(synchronizer._on_despawned)

func get_spawners(node: Node) -> Array[MultiplayerSpawner]:
	var typed_spawners: Array[MultiplayerSpawner] = []
	typed_spawners.assign(node.find_children("*", "MultiplayerSpawner"))
	return typed_spawners
