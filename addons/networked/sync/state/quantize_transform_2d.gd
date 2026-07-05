@tool
## [NetwQuantize] that composes [Transform2D] from origin, rotation, and
## optional scale quantizers.
##
## [member origin_quantizer] writes [member Transform2D.origin].
## [member rotation_quantizer] writes [method Transform2D.get_rotation].
## [member scale_quantizer] writes [method Transform2D.get_scale] when present.
## Skew is not encoded.
##
## [codeblock]
## var q := NetwQuantizeTransform2D.new()
## q.origin_quantizer = NetwQuantizeFixed.new()
## q.rotation_quantizer = NetwQuantizeAngle.new()
## [/codeblock]
class_name NetwQuantizeTransform2D
extends NetwQuantize

## Quantizer for [member Transform2D.origin].
@export var origin_quantizer: NetwQuantize
## Quantizer for [method Transform2D.get_rotation].
@export var rotation_quantizer: NetwQuantize
## Optional quantizer for [method Transform2D.get_scale].
@export var scale_quantizer: NetwQuantize


func _init() -> void:
	if origin_quantizer == null:
		origin_quantizer = NetwQuantizeFixed.new()
	if rotation_quantizer == null:
		rotation_quantizer = NetwQuantizeAngle.new()


func supports_type(type: Variant.Type) -> bool:
	return type == TYPE_TRANSFORM2D


## Implements [method NetwQuantize.write], packing a decomposed [Transform2D].
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeTransform2D: Unsupported type %s." % type_string(
			typeof(value),
		),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform2D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform2D: Missing rotation.",
	)
	var transform: Transform2D = value
	origin_quantizer.write(w, transform.origin)
	rotation_quantizer.write(w, transform.get_rotation())
	if scale_quantizer:
		scale_quantizer.write(w, transform.get_scale())


## Implements [method NetwQuantize.read], reconstructing a [Transform2D].
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		supports_type(type),
		"NetwQuantizeTransform2D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform2D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform2D: Missing rotation.",
	)
	var origin: Vector2 = origin_quantizer.read(r, TYPE_VECTOR2)
	var rotation: float = rotation_quantizer.read(r, TYPE_FLOAT)
	if scale_quantizer:
		var scale: Vector2 = scale_quantizer.read(r, TYPE_VECTOR2)
		return Transform2D(rotation, scale, 0.0, origin)
	return Transform2D(rotation, origin)


## Implements [method NetwQuantize.bit_width], summing composed widths.
func bit_width(type: Variant.Type) -> int:
	assert(
		supports_type(type),
		"NetwQuantizeTransform2D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform2D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform2D: Missing rotation.",
	)
	var total := origin_quantizer.bit_width(TYPE_VECTOR2)
	total += rotation_quantizer.bit_width(TYPE_FLOAT)
	if scale_quantizer:
		total += scale_quantizer.bit_width(TYPE_VECTOR2)
	return total


## Implements [method NetwQuantize.max_error], summing composed error bounds.
func max_error(type: Variant.Type) -> float:
	assert(
		supports_type(type),
		"NetwQuantizeTransform2D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform2D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform2D: Missing rotation.",
	)
	var total := origin_quantizer.max_error(TYPE_VECTOR2)
	total += rotation_quantizer.max_error(TYPE_FLOAT)
	if scale_quantizer:
		total += scale_quantizer.max_error(TYPE_VECTOR2)
	return total
