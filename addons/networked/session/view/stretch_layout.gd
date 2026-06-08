## Pure-function layout solver for HostSceneView.
##
## Translates a [StretchSettings] + control rect into concrete values to push
## onto a [SubViewport]: its [code]size[/code], its [code]size_2d_override[/code]
## and stretch flag, and the on-screen rect ([member Result.inner_rect]) where
## its texture should be drawn. Mirrors Godot's root stretch pipeline.
##
## Knows nothing about networking, multiplayer, or SubViewports. Operates on
## values only, so it's trivially unit-testable in isolation.
class_name StretchLayout
extends RefCounted

class Result extends RefCounted:
	## What to assign to [member SubViewport.size].
	var target_size: Vector2i = Vector2i.ZERO
	## What to assign to [member SubViewport.size_2d_override].
	## [code]Vector2i.ZERO[/code] means leave override disabled.
	var size_2d_override: Vector2i = Vector2i.ZERO
	## What to assign to [member SubViewport.size_2d_override_stretch].
	var override_stretch: bool = false
	## Where, in the parent control's local space, the SubViewport texture
	## should be drawn. Pixels outside this rect form the letterbox.
	var inner_rect: Rect2 = Rect2()


static func compute(s: StretchSettings, control_size: Vector2) -> Result:
	var r := Result.new()

	if control_size.x <= 0.0 or control_size.y <= 0.0:
		r.target_size = Vector2i(
			maxi(1, int(control_size.x)),
			maxi(1, int(control_size.y)),
		)
		r.inner_rect = Rect2(Vector2.ZERO, control_size)
		return r

	# Disabled or missing design size: degenerate to 1:1, no override.
	if s.mode == StretchSettings.Mode.DISABLED \
			or s.design_size.x <= 0 or s.design_size.y <= 0:
		r.target_size = Vector2i(control_size.ceil())
		r.inner_rect = Rect2(Vector2.ZERO, control_size)
		return r

	var scale := maxf(s.scale, 0.0001)
	var effective_design := Vector2(s.design_size) / scale
	var inner_rect := Rect2(Vector2.ZERO, control_size)

	# Aspect adjusts either the inner rect (letterbox) or the effective
	# design (expand/keep_width/keep_height grow the logical viewport).
	match s.aspect:
		StretchSettings.Aspect.IGNORE:
			pass # stretch to fill, design unchanged
		StretchSettings.Aspect.KEEP:
			inner_rect = _aspect_fit(effective_design, control_size)
		StretchSettings.Aspect.KEEP_WIDTH:
			effective_design = Vector2(
				effective_design.x,
				effective_design.x * control_size.y / control_size.x,
			)
		StretchSettings.Aspect.KEEP_HEIGHT:
			effective_design = Vector2(
				effective_design.y * control_size.x / control_size.y,
				effective_design.y,
			)
		StretchSettings.Aspect.EXPAND:
			var cover := minf(
				control_size.x / effective_design.x,
				control_size.y / effective_design.y,
			)
			effective_design = control_size / cover

	# Integer scale: only meaningful when rendering at design res (viewport
	# mode) and only when an integer multiple actually fits.
	if s.scale_mode == StretchSettings.ScaleMode.INTEGER \
			and s.mode == StretchSettings.Mode.VIEWPORT \
			and s.aspect != StretchSettings.Aspect.IGNORE \
			and s.aspect != StretchSettings.Aspect.EXPAND \
			and effective_design.x > 0.0 and effective_design.y > 0.0:
		var ratio := minf(
			inner_rect.size.x / effective_design.x,
			inner_rect.size.y / effective_design.y,
		)
		var k := maxf(1.0, floorf(ratio))
		var snapped := effective_design * k
		inner_rect = _center(snapped, control_size)

	var design_i := Vector2i(effective_design.round())

	match s.mode:
		StretchSettings.Mode.CANVAS_ITEMS:
			# Render at on-screen pixel size (crisp), expose design as the
			# 2D logical size that cameras / UI see.
			r.target_size = Vector2i(inner_rect.size.ceil())
			r.size_2d_override = design_i
			r.override_stretch = true
			r.inner_rect = inner_rect
		StretchSettings.Mode.VIEWPORT:
			# Render at design resolution (chunky pixels when upscaled),
			# then stretch the texture into the inner rect.
			r.target_size = design_i
			r.size_2d_override = Vector2i.ZERO
			r.override_stretch = false
			r.inner_rect = inner_rect
		_:
			r.target_size = Vector2i(control_size.ceil())
			r.inner_rect = Rect2(Vector2.ZERO, control_size)

	return r


static func _aspect_fit(design: Vector2, control_size: Vector2) -> Rect2:
	var design_aspect := design.x / design.y
	var control_aspect := control_size.x / control_size.y
	var fit_size := control_size
	if control_aspect > design_aspect:
		fit_size = Vector2(control_size.y * design_aspect, control_size.y)
	else:
		fit_size = Vector2(control_size.x, control_size.x / design_aspect)
	return _center(fit_size, control_size)


static func _center(rect_size: Vector2, control_size: Vector2) -> Rect2:
	var pos := ((control_size - rect_size) * 0.5).floor()
	return Rect2(pos, rect_size)
