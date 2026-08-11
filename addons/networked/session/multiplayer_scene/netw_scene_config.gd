## The typed scene declaration a session registers with [SceneCore].
##
## Core dispatches on this resource type, not on a manager node class, so the
## same declaration reaches the interface whether a [MultiplayerSceneManager]
## authored it or a test rig built it directly. [member initial_scenes] names
## the levels brought online at startup. Every other level activates on demand
## through [method SceneCore.activate].
## [codeblock]
## var config := NetwSceneConfig.new()
## config.initial_scenes = [preload("res://level/lobby.tscn")]
## api.service_install(config)
## [/codeblock]
class_name NetwSceneConfig
extends NetwObjectConfig

## Isolation the scenes this config declares take when their own recipe names
## none.
##
## Isolation is a per-scene fact, so a scene that declares its own through
## [method NetwScriptModel.SceneMarkConfig.isolated] overrides this. The row
## exists for scenes declared as a bare [PackedScene], which have nowhere else
## to say it.
@export var isolation: NetwMultiplayer.SceneIsolation = \
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE

## Scenes activated when the session starts.
@export var initial_scenes: Array[PackedScene] = []

## Authoring declarations keyed by scene name.
var scenes: Dictionary[StringName, PackedScene] = { }

## Optional spawn data keyed by scene name.
var spawn_data: Dictionary[StringName, Variant] = { }

## Optional custom level constructor. When valid, [SceneCore] calls it
## with the activation data instead of instantiating a [PackedScene], so a game
## builds its own level root while replication, membership, and the verbs stay
## unchanged.
##
## Signature: [code]func(data: Variant) -> Node[/code]
var level_spawn_function: Callable
