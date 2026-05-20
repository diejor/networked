## Suppresses [signal Area2D.body_entered], [signal Area2D.body_exited],
## [signal Area3D.body_entered], and [signal Area3D.body_exited] storms
## triggered by [method Node.reparent], the long-standing godot#14578 bug.
##
## Holds the guarded [Node] in [constant Node.PROCESS_MODE_DISABLED] and
## zeroes [member CollisionObject2D.collision_layer],
## [member CollisionObject2D.collision_mask],
## [member CollisionObject3D.collision_layer], and
## [member CollisionObject3D.collision_mask] for the lifetime of the guard.
## The physics server then drops the body from overlap tracking, so the parent
## swap and any intermediate snap-position assignment do not generate phantom
## enter/exit pairs on areas the body briefly touches.
##
## [br][br]
## Construct before the reparent, optionally await [method flush] so the
## physics server evicts the body from source-area overlap caches before the
## parent swap, and call [method release] (or drop the last reference) once the
## body reaches its final destination position. [method release] is idempotent.
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
## stale [signal Area2D.body_entered] or [signal Area3D.body_entered] when the
## body's [signal Node.tree_exiting] / [signal Node.tree_entered] fire during
## reparent. Defensive signal handlers should call [method Node.is_inside_tree]
## before reading body state.
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
## the guard active to let the physics server process the body's
## suppressed state and evict it from cached area overlaps.
func flush(frames: int = 2) -> void:
	var tree := _body.get_tree() if is_instance_valid(_body) else null
	if not tree:
		return
	for i in frames:
		await tree.physics_frame


## Restores the body's prior [member Node.process_mode] and collision masks.
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


## [code]true[/code] while the guard still holds the body in its suppressed
## state. Becomes [code]false[/code] after [method release].
func is_active() -> bool:
	return _prior_layer != _UNSET or _prior_mask != _UNSET
