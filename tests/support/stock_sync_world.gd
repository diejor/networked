## Stock world root for the [MultiplayerSynchronizer] conformance suite.
##
## Registers the probe's spawnable scene in [method Node._ready] with no
## Networked API in sight, so a [StockSyncProbe] added under the spawner's
## spawn path auto-replicates the stock way.
class_name StockSyncWorld
extends Node


func _ready() -> void:
	var spawner := $StockSyncSpawner as MultiplayerSpawner
	if not StockSyncProbe.packed_scene_path.is_empty():
		spawner.add_spawnable_scene(StockSyncProbe.packed_scene_path)
	for path in StockSyncProbe.extra_scene_paths:
		spawner.add_spawnable_scene(path)
