## Suppresses Area2D/3D body_entered/exited signal storms triggered by
## [method Node.reparent] -- the long-standing godot#14578 bug.
##
## Holds [param body] in [constant Node.PROCESS_MODE_DISABLED] and zeroes
## its [code]collision_layer[/code] / [code]collision_mask[/code] for the
## lifetime of the guard. The PhysicsServer then drops the body from its
## overlap tracking, so the parent swap and any intermediate snap-position
## assignment do not generate phantom enter/exit pairs on areas the body
## briefly touches.
##
## [br][br]
## Construct before the reparent, optionally [code]await[/code]
## [method flush] so the PhysicsServer evicts the body from any
## source-area [code]body_map[/code] cache before the parent swap, and
## call [method release] (or simply drop the last reference) once the
## body reaches its final destination position. [method release] is
## idempotent.
##
## [codeblock]
##     var guard := AreaReparentGuard.new(body)
##     await guard.flush()
##     body.reparent(destination)
##     body.global_position = snap_pos
##     await guard.flush()
##     guard.release()
## [/codeblock]
##
## [b]Residual limitation.[/b] Source areas may still emit a single
## stale [signal Area2D.body_entered] from their cached body_map when
## the body's [signal Node.tree_exiting] / [signal Node.tree_entered]
## fire during reparent. Defensive signal handlers should check
## [code]body.is_inside_tree()[/code] before reading body state.
##
## See https://github.com/godotengine/godot/issues/14578.
class_name AreaReparentGuard
extends RefCounted

const _UNSET: int = -1

var _body: Node
var _prior_mode: int
var _prior_layer: int = _UNSET
var _prior_mask: int = _UNSET


func _init(body: Node) -> void:
	_body = body
	_prior_mode = body.process_mode
	body.process_mode = Node.PROCESS_MODE_DISABLED
	if &"collision_layer" in body:
		_prior_layer = int(body.get(&"collision_layer"))
		body.set(&"collision_layer", 0)
	if &"collision_mask" in body:
		_prior_mask = int(body.get(&"collision_mask"))
		body.set(&"collision_mask", 0)


## Yields [param frames] [signal SceneTree.physics_frame]s. Call with
## the guard active to let the PhysicsServer process the body's
## suppressed state and evict it from cached area overlaps.
func flush(frames: int = 2) -> void:
	var tree := _body.get_tree() if is_instance_valid(_body) else null
	if not tree:
		return
	for i in frames:
		await tree.physics_frame


## Restores the body's prior process mode and collision masks.
## Safe to call more than once; subsequent calls are no-ops.
func release() -> void:
	if not is_instance_valid(_body):
		_prior_layer = _UNSET
		_prior_mask = _UNSET
		return
	if _prior_layer != _UNSET:
		_body.set(&"collision_layer", _prior_layer)
		_prior_layer = _UNSET
	if _prior_mask != _UNSET:
		_body.set(&"collision_mask", _prior_mask)
		_prior_mask = _UNSET
	if _body.process_mode != _prior_mode:
		_body.process_mode = _prior_mode


## [code]true[/code] while the guard still holds the body in its
## suppressed state. Becomes [code]false[/code] after [method release].
func is_active() -> bool:
	return _prior_layer != _UNSET or _prior_mask != _UNSET
