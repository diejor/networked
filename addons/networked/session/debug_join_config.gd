@tool
class_name DebugJoinConfig
extends Resource
## Editor authored stand in for a [JoinPayload] that auto connects a
## [MultiplayerTree] in debug builds.
##
## The join intent stays coherent with the server because [member spawn_point] is
## converted to the same typed join args the live client sends through
## [method NetwDefaultJoin.args_from_scene_node_path]. Author it against a
## template the server's join handler resolves.
## [codeblock]
## # On a debug MultiplayerTree, assign a DebugJoinConfig and the tree hosts
## # straight into the game on play, skipping ConnectBrowser.
## debug_join.username = &"Dev"
## debug_join.spawn_point = SceneNodePath.new("uid://arena::Player")
##
## var payload := debug_join.to_payload()            # is_debug == true
## [/codeblock]

## Display name handed to [member JoinPayload.username] for the auto connected
## player.
@export var username: StringName = &"DebugPlayer"

## The player template a joining player spawns at, converted into
## [member JoinPayload.arg_values]. Leave it [code]null[/code] to express no join
## intent.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:Node")
var spawn_point: SceneNodePath


## Builds the [JoinPayload] the tree submits, with [member JoinPayload.is_debug]
## set so the session can tell debug joins apart.
## [codeblock]
## var payload := debug_join.to_payload()
## await tree.host(payload)
## [/codeblock]
func to_payload() -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	payload.arg_values = NetwDefaultJoin.args_from_scene_node_path(spawn_point)
	payload.is_debug = true
	return payload
