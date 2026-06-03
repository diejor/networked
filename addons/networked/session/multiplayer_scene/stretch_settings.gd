## Resolved 2D stretch configuration for a [HostSceneView].
##
## Mirrors the project-level [code]display/window/stretch/*[/code] knobs Godot
## applies to the root viewport, packaged so the host's SubViewport path can
## reproduce them. Use [method from_project] to read the current project
## settings, or instantiate directly and set fields to override per-view.
class_name StretchSettings
extends Resource

enum Mode { DISABLED, CANVAS_ITEMS, VIEWPORT }
enum Aspect { IGNORE, KEEP, KEEP_WIDTH, KEEP_HEIGHT, EXPAND }
enum ScaleMode { FRACTIONAL, INTEGER }

@export var mode: Mode = Mode.DISABLED
@export var aspect: Aspect = Aspect.KEEP
@export var scale: float = 1.0
@export var scale_mode: ScaleMode = ScaleMode.FRACTIONAL
@export var design_size: Vector2i = Vector2i.ZERO


static func from_project() -> StretchSettings:
	var s := StretchSettings.new()
	s.mode = _mode_from_string(
		str(
			ProjectSettings.get_setting(
				"display/window/stretch/mode",
				"disabled",
			),
		),
	)
	s.aspect = _aspect_from_string(
		str(
			ProjectSettings.get_setting(
				"display/window/stretch/aspect",
				"keep",
			),
		),
	)
	s.scale = float(
		ProjectSettings.get_setting(
			"display/window/stretch/scale",
			1.0,
		),
	)
	s.scale_mode = _scale_mode_from_string(
		str(
			ProjectSettings.get_setting(
				"display/window/stretch/scale_mode",
				"fractional",
			),
		),
	)
	s.design_size = Vector2i(
		int(
			ProjectSettings.get_setting(
				"display/window/size/viewport_width",
				0,
			),
		),
		int(
			ProjectSettings.get_setting(
				"display/window/size/viewport_height",
				0,
			),
		),
	)
	return s


static func _mode_from_string(v: String) -> Mode:
	match v:
		"canvas_items", "2d":
			return Mode.CANVAS_ITEMS
		"viewport":
			return Mode.VIEWPORT
		_:
			return Mode.DISABLED


static func _aspect_from_string(v: String) -> Aspect:
	match v:
		"ignore":
			return Aspect.IGNORE
		"keep_width":
			return Aspect.KEEP_WIDTH
		"keep_height":
			return Aspect.KEEP_HEIGHT
		"expand":
			return Aspect.EXPAND
		_:
			return Aspect.KEEP


static func _scale_mode_from_string(v: String) -> ScaleMode:
	match v:
		"integer":
			return ScaleMode.INTEGER
		_:
			return ScaleMode.FRACTIONAL
