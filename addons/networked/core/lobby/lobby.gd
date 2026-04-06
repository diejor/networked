## Container node representing a single game lobby (server or client variant).
##
## Created by [MultiplayerLobbyManager] via its spawn function. Holds the instantiated
## level scene and wires up spawn/despawn signals to the [LobbySynchronizer].
class_name Lobby
extends Node

## The [LobbySynchronizer] that manages peer visibility for this lobby.
@export var synchronizer: LobbySynchronizer

## The instantiated level scene for this lobby.
##
## Setting this property adds the level as a child, names the lobby, and hooks spawn signals.
var level: Node:
	set(value):
		assert(not is_instance_valid(level))
		level = value
		name = level.name + name
		add_child(level)
		hook_spawn_signals(level)
		level.owner = self


## Connects all root-level [MultiplayerSpawner]s in [param level] to the [member synchronizer].
func hook_spawn_signals(level: Node) -> void:
	var spawners := get_spawners(level)
	for spawner in spawners:
		spawner.spawned.connect(synchronizer._on_spawned)
		spawner.despawned.connect(synchronizer._on_despawned)


## Returns all [MultiplayerSpawner]s in [param node] whose [member MultiplayerSpawner.spawn_path] points to [param node] directly.
func get_spawners(node: Node) -> Array[MultiplayerSpawner]:
	var spawners: Array[MultiplayerSpawner] = []
	spawners.assign(node.find_children("*", "MultiplayerSpawner"))
	return spawners.filter(func(spawner: MultiplayerSpawner) -> bool:
		return spawner.get_path_to(level) == spawner.spawn_path
	)


## Registers [param player] with the synchronizer and adds it to the level scene.
func add_player(player: Node) -> void:
	synchronizer.track_player(player)
	level.add_child(player)
	player.owner = level
