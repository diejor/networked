@tool
class_name DebugJoinConfig
extends Resource
## Editor authored stand in for a [JoinPayload] that auto connects a
## [MultiplayerTree] in debug builds.
##
## The spawn intent stays coherent with the server because it is produced by a
## real [SpawnPolicy] instance through [method SpawnPolicy.to_dict], the exact
## call the live client makes. Author [member spawn] as an instance of whatever
## policy class [member MultiplayerTree.spawn_policy] is, and the server cannot
## receive a dictionary it does not understand.
## [codeblock]
## # On a debug MultiplayerTree, assign a DebugJoinConfig and the tree hosts
## # straight into the game on play, skipping ConnectBrowser.
## debug_join.username = &"Dev"
## debug_join.spawn = EntitySpawnPolicy.new()   # same class as the server
##
## var payload := debug_join.to_payload()            # is_debug == true
## [/codeblock]

## Display name handed to [member JoinPayload.username] for the auto connected
## player.
@export var username: StringName = &"DebugPlayer"

## Client side [SpawnPolicy] whose [method SpawnPolicy.to_dict] fills
## [member JoinPayload.spawn]. Leave it [code]null[/code] to express no spawn
## intent, which suits a [code]null[/code]
## [member MultiplayerTree.spawn_policy].
@export var spawn: SpawnPolicy


## Builds the [JoinPayload] the tree submits, with [member JoinPayload.is_debug]
## set so the session can tell debug joins apart.
## [codeblock]
## var payload := debug_join.to_payload()
## await tree.host_player(payload)
## [/codeblock]
func to_payload() -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	payload.spawn = spawn.to_dict() if spawn else { }
	payload.is_debug = true
	return payload
