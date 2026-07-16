@tool
## Per value interpolation spec used by [NetwInterpolationInterface].
##
## [NetwInterpolate] names how one decoded value enters the interpolation
## history. [member target] names the property that receives the smoothed
## output. [member mode], [member smoothing], and [member snap_distance] are
## read by [method NetwInterpolationInterface.record].
## [codeblock]
## Netw.configure_property(self, &"position").interpolate(
##     NetwInterpolate.new().lerp().smooth(0.05).to(&"position")
## )
## [/codeblock]
##
## The same resource shape is used by [method Netw.configure_property],
## [method Netw.configure_rpc], and [method Netw.configure_signal].
##
## When [member NetwInterpolationInterface.Handle.visual_root] is set the visual
## keeps inheriting the body transform and spatial values are written in global
## space, so a smoothed channel is not dragged by body writes. Add a second
## [NetwInterpolate] to also smooth [code]rotation[/code].
class_name NetwInterpolate
extends Resource

## Defines the interpolation algorithm for one value.
enum Mode {
	## No interpolation.
	NONE = 0,
	## Linear interpolation through [method @GlobalScope.lerp].
	LERP = 1,
	## Angular interpolation through [method @GlobalScope.lerp_angle].
	ANGLE = 2,
	## Spherical interpolation for [Quaternion] rotations.
	SLERP = 3,
}

## Interpolation algorithm used for this value.
@export var mode: Mode = Mode.LERP

## Exponential smoothing time in seconds layered onto bracketed interpolation.
@export_custom(0, "suffix:s") var smoothing: float = 0.05

## Distance that snaps instead of interpolating. [code]0.0[/code] disables it.
@export var snap_distance: float = 0.0

## Property receiving the smoothed output.
##
## Empty means the source property name for [method Netw.configure_property].
## RPC and signal arguments should set an explicit [member target].
@export var target: StringName = &""


## Returns [code]true[/code] when this spec can smooth [param type].
func _supports_type(type: Variant.Type) -> bool:
	return type in [
		TYPE_FLOAT,
		TYPE_VECTOR2,
		TYPE_VECTOR3,
		TYPE_QUATERNION,
		TYPE_COLOR,
	]


## Selects [constant Mode.NONE].
func none() -> NetwInterpolate:
	mode = Mode.NONE
	return self


## Selects [constant Mode.LERP].
func lerp() -> NetwInterpolate:
	mode = Mode.LERP
	return self


## Selects [constant Mode.ANGLE].
func angle() -> NetwInterpolate:
	mode = Mode.ANGLE
	return self


## Selects [constant Mode.SLERP].
func slerp() -> NetwInterpolate:
	mode = Mode.SLERP
	return self


## Sets [member smoothing] to [param seconds].
func smooth(seconds: float) -> NetwInterpolate:
	smoothing = seconds
	return self


## Sets [member snap_distance] to [param distance].
func snap_at(distance: float) -> NetwInterpolate:
	snap_distance = distance
	return self


## Sets [member target] to [param property].
func to(property: StringName) -> NetwInterpolate:
	target = property
	return self
