class_name Lobby
extends Node

@export var synchronizer: LobbySynchronizer

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
	for spawner in spawners:
		spawner.spawned.connect(synchronizer._on_spawned)
		spawner.despawned.connect(synchronizer._on_despawned)

func get_spawners(node: Node) -> Array[MultiplayerSpawner]:
	var spawners: Array[MultiplayerSpawner] = []
	spawners.assign(node.find_children("*", "MultiplayerSpawner"))
	return spawners.filter(func(spawner: MultiplayerSpawner) -> bool:
		return spawner.get_path_to(level) == spawner.spawn_path
	)

func add_player(player: Node) -> void:
	synchronizer.track_player(player)
	level.add_child(player)
	player.owner = level
