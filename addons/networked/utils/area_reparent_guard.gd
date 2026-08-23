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
extends RefCounted

const _UNSET: int = -1

var _body: Node
var _prior_mode: int
var _prior_layer: int = _UNSET
var _prior_mask: int = _UNSET
# The open flush_then window: the tree its frames are counted in, how many are
# left, and who it answers. Held here rather than bound into the connection,
# because disconnecting has to name the same Callable and a bound one is a
# fresh object every time it is built.
var _window_tree: SceneTree
var _window_frames: int = 0
var _window_answer: Callable
# The guard owns itself while a window is open, because nothing else has to: the
# caller handed its continuation over and went on, and a signal connection does
# not keep a RefCounted alive. Closing the window is what lets the guard go.
var _window_self: RefCounted


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


## Calls [param answered] after [param frames] [signal SceneTree.physics_frame]s
## have passed, or right away when the body has no [SceneTree] to count them in.
##
## The window [method flush] awaits, offered to a caller that cannot await one.
## The count stays on [signal SceneTree.physics_frame] because only a physics
## step makes the physics server drop the suppressed body from its area overlap
## caches, so a window counted in any other cadence would let the reparent land
## while the source areas still track the body, which is the phantom
## enter/exit pair this whole class exists to suppress.
## One window is open at a time: opening a second replaces the first, which
## then never answers.
## [codeblock]
##     var guard := AreaReparentGuard.new(body)
##     guard.flush_then(func() -> void:
##         body.reparent(destination)
##         guard.flush_then(guard.release)
##     )
## [/codeblock]
func flush_then(answered: Callable, frames: int = 2) -> void:
	_close_window()
	var tree := _body.get_tree() if is_instance_valid(_body) else null
	if not tree or frames <= 0:
		answered.call()
		return
	_window_tree = tree
	_window_frames = frames
	_window_answer = answered
	_window_self = self
	tree.physics_frame.connect(_spend_window_frame)


# Spends one physics frame of the open window, answering on the last of them.
# The answer runs after the window is closed, so answering by opening the next
# window is what the second flush_then of a reparent is. Closing drops the
# guard's hold on itself, which is why the hold is retaken as a local first: an
# answer that opens no further window would otherwise free the guard while this
# call is still inside it.
func _spend_window_frame() -> void:
	_window_frames -= 1
	if _window_frames > 0:
		return
	@warning_ignore("unused_variable")
	var held: RefCounted = self
	var answered := _window_answer
	_close_window()
	answered.call()


# Ends the open window without answering it.
func _close_window() -> void:
	if _window_tree and _window_tree.physics_frame.is_connected(
			_spend_window_frame,
	):
		_window_tree.physics_frame.disconnect(_spend_window_frame)
	_window_tree = null
	_window_frames = 0
	_window_answer = Callable()
	_window_self = null


## Restores the body's [member Node.process_mode] while keeping its collision
## masks suppressed. Lets a body resume processing (input, physics, a following
## camera) during a reveal without regaining physics-server overlap, so a later
## [method release] still clears the phantom-signal window. Idempotent.
func resume_processing() -> void:
	if is_instance_valid(_body) and _body.process_mode != _prior_mode:
		_body.process_mode = _prior_mode


## Restores the body's prior [member Node.process_mode] and collision masks.
## Safe to call more than once. Subsequent calls are no-ops.
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
