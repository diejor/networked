## Draws one [SubViewport] into a [Control] and forwards input to it.
##
## [method set_target] owns the target render state while attached.
## [method forward_input] lets a container route keyboard and joypad input
## without enabling automatic unhandled input on every view.
class_name ParticipantView
extends Control

## Optional per-view stretch configuration. When [code]null[/code], settings
## are read from [ProjectSettings].
@export var stretch_override: StretchSettings = null

## When enabled, non-mouse [method Control._unhandled_input] events are pushed
## into [method set_target]. [HostSceneView] enables this for standalone use.
var forward_unhandled_input := false

# Meta key marking which view currently borrows a target's render state.
const _OWNER_META := &"_participant_view_owner"

var _target: SubViewport = null
var _settings: StretchSettings = null
var _layout: StretchLayout.Result = null
var _previous_update_mode: int = SubViewport.UPDATE_DISABLED
var _previous_clear_mode: int = SubViewport.CLEAR_MODE_NEVER
var _previous_size_2d_override: Vector2i = Vector2i.ZERO
var _previous_override_stretch := false
var _dbg: NetwHandle = Netw.dbg.handle(self)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	mouse_filter = Control.MOUSE_FILTER_STOP
	set_process(false)
	if not resized.is_connected(_on_resized):
		resized.connect(_on_resized)


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_settings = stretch_override if stretch_override else StretchSettings.from_project()
	_sync_root_sized_rect()
	var viewport := get_viewport()
	if viewport and not viewport.size_changed.is_connected(_sync_root_sized_rect):
		viewport.size_changed.connect(_sync_root_sized_rect)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var viewport := get_viewport()
	if viewport and viewport.size_changed.is_connected(_sync_root_sized_rect):
		viewport.size_changed.disconnect(_sync_root_sized_rect)
	clear_target()


func _process(_dt: float) -> void:
	if is_instance_valid(_target):
		queue_redraw()


## Points this view at [param viewport] and forces it to render every frame.
##
## Restores the previous viewport's render settings. Pass [code]null[/code]
## or call [method clear_target] to detach.
func set_target(viewport: SubViewport) -> void:
	if _target == viewport:
		return
	if is_instance_valid(_target):
		_release_target_ownership()
		_restore_target_render_state()
		if _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.disconnect(_on_target_freed)

	_target = viewport

	if is_instance_valid(_target):
		_claim_target_ownership()
		_previous_update_mode = _target.render_target_update_mode
		_previous_clear_mode = _target.render_target_clear_mode
		_previous_size_2d_override = _target.size_2d_override
		_previous_override_stretch = _target.size_2d_override_stretch
		_target.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_target.render_target_clear_mode = SubViewport.CLEAR_MODE_ALWAYS
		_apply_layout()
		if not _target.tree_exiting.is_connected(_on_target_freed):
			_target.tree_exiting.connect(_on_target_freed)
			_dbg.info("%s now displays '%s'.", [name, _target.name])

	set_process(is_instance_valid(_target))
	queue_redraw()


## Detaches this view from its current target.
func clear_target() -> void:
	set_target(null)


## Pushes [param event] into the current target viewport.
func forward_input(event: InputEvent) -> void:
	if not is_instance_valid(_target):
		return
	_target.push_input(event, true)


func _on_target_freed() -> void:
	if is_instance_valid(_target):
		_release_target_ownership()
		_restore_target_render_state()
	_target = null
	set_process(false)
	queue_redraw()


# Claims exclusive render ownership of the current target. Two views borrowing
# the same SubViewport would corrupt each other's saved render state, so this
# asserts the single-owner invariant before flipping the target's render mode.
func _claim_target_ownership() -> void:
	if _target.has_meta(_OWNER_META):
		var owner := instance_from_id(_target.get_meta(_OWNER_META))
		assert(
			not is_instance_valid(owner) or owner == self,
			(
					"ParticipantView: '%s' is already displayed by another view. "
					+ "A SubViewport can be owned by only one ParticipantView."
			) % _target.name,
		)
	_target.set_meta(_OWNER_META, get_instance_id())


func _release_target_ownership() -> void:
	if _target.has_meta(_OWNER_META) \
			and _target.get_meta(_OWNER_META) == get_instance_id():
		_target.remove_meta(_OWNER_META)


func _restore_target_render_state() -> void:
	_target.render_target_update_mode = _previous_update_mode
	_target.render_target_clear_mode = _previous_clear_mode
	_target.size_2d_override = _previous_size_2d_override
	_target.size_2d_override_stretch = _previous_override_stretch


func _on_resized() -> void:
	_apply_layout()


func _apply_layout() -> void:
	if _settings == null:
		_settings = stretch_override if stretch_override else StretchSettings.from_project()
	_layout = StretchLayout.compute(_settings, size)
	if is_instance_valid(_target):
		_target.size = _layout.target_size
		_target.size_2d_override = _layout.size_2d_override
		_target.size_2d_override_stretch = _layout.override_stretch
	queue_redraw()


# Makes the view fill the game window when parented under a plain Node.
func _sync_root_sized_rect() -> void:
	if get_parent() is Control:
		return
	var viewport := get_viewport()
	if not viewport:
		return
	var rect := viewport.get_visible_rect()
	anchor_left = 0.0
	anchor_top = 0.0
	anchor_right = 0.0
	anchor_bottom = 0.0
	position = Vector2.ZERO
	set_deferred("size", rect.size)


func _draw() -> void:
	if not is_instance_valid(_target) or _layout == null:
		return
	var tex := _target.get_texture()
	if tex:
		draw_texture_rect(tex, _layout.inner_rect, false)


# Remaps local mouse coordinates into the target viewport's logical space.
func _gui_input(event: InputEvent) -> void:
	if not is_instance_valid(_target) or _layout == null:
		return
	var to_push := event
	if event is InputEventMouse and _layout.inner_rect.size.x > 0.0 \
			and _layout.inner_rect.size.y > 0.0:
		var logical := Vector2(_target.size_2d_override) \
		if _target.size_2d_override != Vector2i.ZERO \
		else Vector2(_target.size)
		var scale := logical / _layout.inner_rect.size
		var xform := Transform2D.IDENTITY.scaled(scale) \
				.translated(-_layout.inner_rect.position * scale)
		to_push = event.xformed_by(xform)
	forward_input(to_push)
	accept_event()


func _unhandled_input(event: InputEvent) -> void:
	if not forward_unhandled_input:
		return
	if event is InputEventMouse:
		return
	forward_input(event)
