@tool
@abstract
class_name SpawnPolicy
extends Resource
## Server-side strategy for spawning a player once their join is accepted.
##
## Assign it to [member MultiplayerTree.spawn_policy]. A [code]null[/code]
## policy means the session does not auto-spawn, and gameplay drives
## [signal MultiplayerTree.player_joined] itself. [EntitySpawnPolicy] is
## the built-in default.
##
## [br][br]
## A policy spans two peers. The joining client fills the exported fields and
## serializes them through [method to_dict] into [member JoinPayload.spawn].
## The server reads that same data back from [member ResolvedJoin.spawn] in
## [method spawn] and creates the player.
## [codeblock]
## # A custom policy that spawns the player at a named point.
## class_name SpawnAtPoint
## extends SpawnPolicy
##
## @export var point_name: StringName
##
## func to_dict() -> Dictionary:
##     return { "point": point_name }
##
## func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene:
##     var point: StringName = rj.spawn.get("point", &"")
##     var mgr := ctx.services.get_scene_manager()
##     var scene := await mgr.activate_scene(&"Arena")
##     # ...add the player to scene at point...
##     return scene
## [/codeblock]

## Spawns the player for the accepted join [param rj] and returns
## the [MultiplayerScene] they entered, or [code]null[/code]. The tree emits
## [signal MultiplayerTree.player_scene_ready] with that scene.
##
## Read the client's spawn intent from [member ResolvedJoin.spawn] and reach
## scene services through [param ctx].
## [codeblock]
## func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene:
##     var point: StringName = rj.spawn.get("point", &"")
##     var mgr := ctx.services.get_scene_manager()
##     var scene := await mgr.activate_scene(&"Arena")
##     # ...add the player to scene at point...
##     return scene
## [/codeblock]
## [br][br][b]Server Only.[/b]
@abstract
func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene


## Serializes this policy's exported fields into the spawn-intent dictionary
## the client stores in [member JoinPayload.spawn]. Mirror the keys
## [method spawn] reads back.
## [codeblock]
## func to_dict() -> Dictionary:
##     return { "point": point_name }
## [/codeblock]
@abstract
func to_dict() -> Dictionary
