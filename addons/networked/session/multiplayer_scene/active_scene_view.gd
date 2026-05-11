## Renders an active [SubViewport] scene into the host's root viewport.
##
## Add this as a child of [MultiplayerTree] in your main scene. On
## listen-server hosts, [MultiplayerSceneManager] resolves it via
## [NetwServices] and points it at the local player's current scene each
## time the player enters or teleports between scenes.
## [br][br]
## Pure clients and dedicated servers do not need this node — pure clients
## render their scene directly into root, and dedicated servers don't render
## at all.
## [br][br]
## [b]Layout:[/b] defaults to filling its parent rect (PRESET_FULL_RECT).
## Override anchors after adding it if you want a partial-screen view.
class_name ActiveSceneView
extends Control
 
## If [code]true[/code], the target [SubViewport]'s size is kept in sync with
## this control's size. Disable to render at a fixed (e.g. lower) resolution.
@export var auto_resize_target: bool = true

var _target: SubViewport = null
var _previous_update_mode: int = SubViewport.UPDATE_DISABLED
var _previous_clear_mode: int = SubViewport.CLEAR_MODE_NEVER
var _dbg: NetwHandle = Netw.dbg.handle(self)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	NetwServices.register(self, ActiveSceneView)
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(_on_resized)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	clear_target()
	NetwServices.unregister(self, ActiveSceneView)


## Points this view at [param viewport] and forces it to render every frame.
##
## Restores the previous viewport's render settings. Pass [code]null[/code]
## or call [method clear_target] to detach.
func set_target(viewport: SubViewport) -> void:
	if _target == viewport:
		return
	if is_instance_valid(_target):
		_restore_target_render_state()
		if _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.disconnect(_on_target_freed)

	_target = viewport

	if is_instance_valid(_target):
		_previous_update_mode = _target.render_target_update_mode
		_previous_clear_mode = _target.render_target_clear_mode
		_target.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_target.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
		if auto_resize_target:
			_target.size = Vector2i(size)
		if not _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.connect(_on_target_freed)
		_dbg.info("ActiveSceneView now displays '%s'.", [_target.name])

	queue_redraw()


## Detaches this view from its current target.
func clear_target() -> void:
	set_target(null)


func _draw() -> void:
	if not is_instance_valid(_target):
		return
	var tex := _target.get_texture()
	if tex:
		draw_texture_rect(tex, Rect2(Vector2.ZERO, size), false)


# Forwards GUI input to the target SubViewport. Events from _gui_input are
# already in this control's local coordinate space; when target.size matches
# this control's size, no remap is needed. When sizes differ, scale position.
func _gui_input(event: InputEvent) -> void:
	if not is_instance_valid(_target):
		return
	var to_push := event
	if event is InputEventMouse and not auto_resize_target \
			and Vector2(_target.size) != size and size.x > 0 and size.y > 0:
		var scale := Vector2(_target.size) / size
		var xform := Transform2D.IDENTITY.scaled(scale)
		to_push = event.xformed_by(xform)
	_target.push_input(to_push, true)


func _on_resized() -> void:
	if auto_resize_target and is_instance_valid(_target):
		_target.size = Vector2i(size)


func _on_target_freed() -> void:
	if is_instance_valid(_target):
		_restore_target_render_state()
	_target = null
	queue_redraw()


func _restore_target_render_state() -> void:
	_target.render_target_update_mode = _previous_update_mode
	_target.render_target_clear_mode = _previous_clear_mode
