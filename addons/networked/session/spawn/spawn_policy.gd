@tool
@abstract
class_name SpawnPolicy
extends Resource
## Server-side strategy for spawning a player once their join is accepted.
##
## Assign it to [member MultiplayerTree.spawn_policy]. A [code]null[/code]
## policy means the session does not auto-spawn, and gameplay drives
## [signal MultiplayerTree.participant_joined] itself. [EntitySpawnPolicy] is
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
##     var mgr := ctx.services.scene_manager
##     var scene := await mgr.activate_scene(&"Arena")
##     # ...add the player to scene at point...
##     return scene
## [/codeblock]

const _POLICY_SCRIPT_KEY := "_spawn_policy_script"


## Returns the script identifier used to detect client and server policy
## mismatches in serialized spawn intent.
func policy_script_identifier() -> String:
	var script := get_script()
	if script == null:
		return ""
	if script.has_method("get_global_name"):
		var global_name := str(script.call("get_global_name"))
		if not global_name.is_empty():
			return global_name
	var path: String = script.resource_path
	return path if not path.is_empty() else str(script)


## Returns [param data] with this policy's script identifier attached.
func with_policy_script(data: Dictionary) -> Dictionary:
	var out := data.duplicate(true)
	out[_POLICY_SCRIPT_KEY] = policy_script_identifier()
	return out


## Spawns the player for the accepted join [param rj] and returns
## the [MultiplayerScene] they entered, or [code]null[/code]. The participant's
## [member NetwParticipant.current_scene] reflects the returned scene.
##
## Read the client's spawn intent from [member ResolvedJoin.spawn] and reach
## scene services through [param ctx].
## [codeblock]
## func spawn(rj: ResolvedJoin, ctx: NetwContext) -> MultiplayerScene:
##     var point: StringName = rj.spawn.get("point", &"")
##     var mgr := ctx.services.scene_manager
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
