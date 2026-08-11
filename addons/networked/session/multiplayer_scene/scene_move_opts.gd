## Options bag for [method NetwMultiplayer.scene_move].
##
## Properties only, no signals and no identity, so the whole thing is inputs to
## one call rather than an object with a lifetime. Passing [code]null[/code]
## takes every default.
## [codeblock]
## var opts := SceneMoveOpts.new()
## opts.preserve_history = true
## opts.target_global_position = spawn_point.global_position
## api.scene_move(player, arena, opts)
## [/codeblock]
class_name SceneMoveOpts
extends RefCounted

## Keeps the entity's lag-compensation timeline across the move instead of
## restarting it at the destination.
var preserve_history := false

## Gameplay label recorded with the move, for diagnostics.
var reason: StringName = &"scene_move"

## Destination world position, or [code]null[/code] to keep the current one. A
## following camera resets its smoothing on
## [signal NetwEntity.reparented] when this is set.
var target_global_position: Variant = null


# Lowers this bag into the registry's own options object. The two carry the
# same fields, and this is the seam where the public POD meets the private core.
func to_move_opts() -> SceneCore.MoveOpts:
	var out := SceneCore.MoveOpts.new()
	out.preserve_history = preserve_history
	out.reason = reason
	out.target_global_position = target_global_position
	return out
