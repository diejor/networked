## The typed scene declaration a session registers with [NetwSceneInterface].
##
## Core dispatches on this resource type, not on a manager node class, so the
## same declaration reaches the interface whether a [MultiplayerSceneManager]
## authored it or a test rig built it directly. [member concurrency] is fixed
## for the session because it selects the wrapper root every peer builds, so it
## cannot change while a session is online. [member initial_scenes] names the
## levels brought online at startup. Every other level activates on demand
## through [method NetwSceneInterface.activate].
## [codeblock]
## var config := NetwSceneConfig.new()
## config.concurrency = NetwSceneConfig.Concurrency.SINGLE
## config.initial_scenes = [preload("res://level/lobby.tscn")]
## api.object_configuration_add(manager_node, config)
## [/codeblock]
class_name NetwSceneConfig
extends Resource

## Whether one or several scenes may be active in the session.
enum Concurrency {
	## At most one scene may be active. All peers use a plain node wrapper.
	SINGLE,
	## Several scenes may be active. Hosting peers isolate their worlds.
	CONCURRENT,
}

## Declared scene concurrency. Immutable while a session is online.
@export var concurrency: Concurrency = Concurrency.SINGLE

## Scenes activated when the session starts.
@export var initial_scenes: Array[PackedScene] = []

## Authoring declarations keyed by scene name.
var scenes: Dictionary[StringName, PackedScene] = { }

## Optional spawn data keyed by scene name.
var spawn_data: Dictionary[StringName, Variant] = { }
