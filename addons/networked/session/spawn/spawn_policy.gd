@tool
class_name SpawnPolicy
extends Resource

## Server-side strategy that turns an accepted [ResolvedJoin] into a spawned
## player. Configured on a [MultiplayerSceneManager] via
## [member MultiplayerSceneManager.spawn_policy]; [code]null[/code] means the
## session does not auto-spawn on join (gameplay handles
## [signal MultiplayerTree.player_joined] itself).
##
## [b]Stateless per join.[/b] A single policy instance is shared across every
## join (and rides along when the tree is duplicated for the server), so
## [method spawn] must read its inputs from [member ResolvedJoin.spawn] into
## locals rather than storing them on the policy. The exported fields exist
## only to author the client-side [member ResolvedJoin.spawn] payload via
## [method to_dict].
##
## [codeblock]
## # Client authoring (e.g. a connect popup):
## payload.spawn = SpawnerComponentPolicy.from_scene_node_path(picked).to_dict()
## [/codeblock]


## Server-only. Spawns the player described by [param rj] using [param mgr]'s
## scene services. Reads spawn intent from [member ResolvedJoin.spawn]; a
## policy that needs no client input may ignore it. Override in a subclass.
func spawn(_rj: ResolvedJoin, _mgr: MultiplayerSceneManager) -> void:
	assert(false, "SpawnPolicy.spawn must be overridden by a subclass.")


## Returns the wire-schema dictionary the client stores in
## [member JoinPayload.spawn]. Override to describe this policy's inputs.
func to_dict() -> Dictionary:
	return {}
