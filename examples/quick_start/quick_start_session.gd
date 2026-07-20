extends Node2D
## Tree-less session bootstrap for the quick_start example.
##
## The example carries no [MultiplayerTree] and no [MultiplayerSceneManager]
## node. This script sits on the scene root and declares the session's scenes as
## a [NetwSceneConfig], so the same authoring the manager once provided rides one
## script. The session is already mounted when this runs, by the harness-wrapped
## tree under test or the [code]networked/install_as_default[/code] autoload at
## startup, so [method Netw.of] always resolves it and the bootstrap never mounts
## or names the session itself.
## [codeblock]
## Main (Node2D)   # this script: config + scene-change listener, no session node
## ┠╴ Player spawner, connect UI, presentation
## ┖╴ (session mounted by the install_as_default autoload, or by the harness)
## [/codeblock]

const LEVEL_1 := preload("res://examples/quick_start/Level1.tscn")
const LEVEL_2 := preload("res://examples/quick_start/Level2.tscn")

var api: NetwMultiplayer:
	get: return multiplayer


func _enter_tree() -> void:
	var config := NetwSceneConfig.new()
	config.concurrency = NetwSceneConfig.Concurrency.CONCURRENT
	config.initial_scenes = [LEVEL_1]
	config.scenes = {
		&"Level1": LEVEL_1,
		&"Level2": LEVEL_2,
	}
	api.object_configuration_add(self, config)
	api.scenes.change_requested.connect(
		func(rq: SceneChangeRequest) -> void:
			if not rq.targets(&"Level2"):
				rq.deny()
	)
