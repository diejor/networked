extends Node
## Tree-less session bootstrap for the bomber example.
##
## The example carries no [MultiplayerTree] and no [MultiplayerSceneManager]
## node. This script sits on the scene root and registers the session's
## [NetwSessionConfig] and [NetwSceneConfig], so the services authored as
## sibling nodes (gamestate, clock, lag compensation, lobby directories) and
## the declared scenes all ride one mounted session. The session is already
## mounted when this runs, by the harness-wrapped tree under test or the
## [code]networked/install_as_default[/code] autoload at startup, so
## [method Netw.of] always resolves it and the bootstrap never mounts or names
## the session itself.
## [codeblock]
## Main (Node)   # this script: session + scene declaration, no session node
## ┠╴ Gamestate, MultiplayerClock, LagCompensation, lobby directories
## ┖╴ Lobby (CanvasLayer)   # pre-session browser shell
## [/codeblock]

const WORLD := preload("res://examples/bomber/game/world.tscn")
const LOBBY_LEVEL := preload("res://examples/bomber/game/lobby_level.tscn")

var api: NetwMultiplayer:
	get: return multiplayer


func _enter_tree() -> void:
	var session := NetwSessionConfig.new()
	session.app_id = &"7x283tsfmy1xpr4"
	api.object_configuration_add(self, session)

	var scenes := NetwSceneConfig.new()
	scenes.initial_scenes = [LOBBY_LEVEL]
	scenes.scenes = {
		&"World": WORLD,
		&"Lobby": LOBBY_LEVEL,
	}
	api.object_configuration_add(self, scenes)
