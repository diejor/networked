extends Node2D

const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")

var api: NetwMultiplayer:
	get: return multiplayer


func _enter_tree() -> void:
	var config := NetwSceneConfig.new()
	config.isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD
	config.initial_scenes = [LEVEL_1]
	config.scenes = {
		&"Level1": LEVEL_1,
		&"Level2": LEVEL_2,
	}
	api.object_configuration_add(self, config)
