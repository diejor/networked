## Isolated window viewport tree for one local participant.
##
## [member tree], [member peer_id], and [member username] identify the
## participant mounted in this window.
class_name ParticipantWindow
extends Window

## Optional window stretch configuration. When [code]null[/code], settings
## are read from [ProjectSettings].
@export var stretch_override: StretchSettings = null

## [MultiplayerTree] mounted inside this window.
var tree: MultiplayerTree:
	get:
		return _tree
	set(value):
		_set_tree(value)

## Network peer id for the mounted participant.
var peer_id: int = 0

## Username used by the mounted participant.
var username: StringName = &""

var _pending_input: Array[InputEvent] = []
var _input_flush_queued := false
var _settings: StretchSettings = null
var _tree: MultiplayerTree = null


func _ready() -> void:
	visible = false
	_apply_stretch_settings()


## Sets this embedded window rect and syncs its content scaling.
func set_tiled_rect(rect: Rect2i) -> void:
	position = rect.position
	size = rect.size
	_apply_stretch_settings()


## Sends [param event] into this window.
##
## Listen server hosts route input through [HostSceneView]. Clients receive it
## through this window viewport directly.
func send_input(event: InputEvent) -> void:
	_pending_input.append(event)
	if _input_flush_queued:
		return
	_input_flush_queued = true
	_flush_input.call_deferred()


func _flush_input() -> void:
	_input_flush_queued = false
	var events := _pending_input.duplicate()
	_pending_input.clear()
	for event in events:
		push_input(event, true)


func _set_tree(value: MultiplayerTree) -> void:
	if _tree == value:
		return
	_tree = value


func _apply_stretch_settings() -> void:
	_settings = (
			stretch_override
			if stretch_override
			else StretchSettings.from_project()
	)
	content_scale_mode = _window_scale_mode(_settings.mode)
	content_scale_aspect = _window_scale_aspect(_settings.aspect)
	content_scale_stretch = _window_scale_stretch(_settings.scale_mode)
	content_scale_factor = _settings.scale
	content_scale_size = _settings.design_size


func _window_scale_mode(mode: StretchSettings.Mode) -> ContentScaleMode:
	match mode:
		StretchSettings.Mode.CANVAS_ITEMS:
			return Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
		StretchSettings.Mode.VIEWPORT:
			return Window.CONTENT_SCALE_MODE_VIEWPORT
		_:
			return Window.CONTENT_SCALE_MODE_DISABLED


func _window_scale_aspect(aspect: StretchSettings.Aspect) -> ContentScaleAspect:
	match aspect:
		StretchSettings.Aspect.IGNORE:
			return Window.CONTENT_SCALE_ASPECT_IGNORE
		StretchSettings.Aspect.KEEP_WIDTH:
			return Window.CONTENT_SCALE_ASPECT_KEEP_WIDTH
		StretchSettings.Aspect.KEEP_HEIGHT:
			return Window.CONTENT_SCALE_ASPECT_KEEP_HEIGHT
		StretchSettings.Aspect.EXPAND:
			return Window.CONTENT_SCALE_ASPECT_EXPAND
		_:
			return Window.CONTENT_SCALE_ASPECT_KEEP


func _window_scale_stretch(
		scale_mode: StretchSettings.ScaleMode,
) -> ContentScaleStretch:
	match scale_mode:
		StretchSettings.ScaleMode.INTEGER:
			return Window.CONTENT_SCALE_STRETCH_INTEGER
		_:
			return Window.CONTENT_SCALE_STRETCH_FRACTIONAL
