extends Node2D

const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")

var api: NetwMultiplayer:
	get: return multiplayer


func _enter_tree() -> void:
	var config := NetwSceneConfig.new()
	config.isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD
	config.declare_scene(&"Level1", LEVEL_1, true)
	config.declare_scene(&"Level2", LEVEL_2)
	api.object_configuration_add(self, config)
