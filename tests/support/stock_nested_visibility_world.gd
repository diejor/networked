## Stock world root for nested visibility conformance.
##
## Registers the test's root scenes on a plain [MultiplayerSpawner] during
## [method Node._ready], matching an unmodified stock project.
class_name StockNestedVisibilityWorld
extends Node

## Packed root scenes registered by [method Node._ready].
static var root_scene_paths: Array[String] = []


func _ready() -> void:
	var spawner := $RootSpawner as MultiplayerSpawner
	for path in root_scene_paths:
		spawner.add_spawnable_scene(path)
