## Stock world root for the [MultiplayerSpawner] conformance suite.
##
## Configures its spawner exactly the way an unmodified stock project does:
## the spawnable scene list and [member MultiplayerSpawner.spawn_function] are
## assigned in [method Node._ready], with no Networked API in sight.
class_name StockWorld
extends Node

func _ready() -> void:
	var spawner := $StockSpawner as MultiplayerSpawner
	if not StockSpawnProbe.packed_scene_path.is_empty():
		spawner.add_spawnable_scene(StockSpawnProbe.packed_scene_path)
	spawner.spawn_function = _spawn_custom


func _spawn_custom(data: Variant) -> Node:
	var probe: StockSpawnProbe = \
			load(StockSpawnProbe.packed_scene_path).instantiate()
	probe.custom_data = str(data)
	return probe
