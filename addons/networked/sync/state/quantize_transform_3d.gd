@tool
## [NetwQuantize] that composes [Transform3D] from origin, rotation, and
## optional scale quantizers.
##
## [member origin_quantizer] writes [member Transform3D.origin].
## [member rotation_quantizer] writes [method Basis.get_rotation_quaternion].
## [member scale_quantizer] writes local [method Basis.get_scale] when present.
## Shear is not encoded.
##
## [codeblock]
## var q := NetwQuantizeTransform3D.new()
## q.origin_quantizer = NetwQuantizeFixed.new()
## q.rotation_quantizer = NetwQuantizeQuaternion.new()
## [/codeblock]
class_name NetwQuantizeTransform3D
extends NetwQuantize

## Quantizer for [member Transform3D.origin].
@export var origin_quantizer: NetwQuantize
## Quantizer for [method Basis.get_rotation_quaternion].
@export var rotation_quantizer: NetwQuantize
## Optional quantizer for [method Basis.get_scale].
@export var scale_quantizer: NetwQuantize


func _init() -> void:
	if origin_quantizer == null:
		origin_quantizer = NetwQuantizeFixed.new()
	if rotation_quantizer == null:
		rotation_quantizer = NetwQuantizeQuaternion.new()


func supports_type(type: Variant.Type) -> bool:
	return type == TYPE_TRANSFORM3D


## Implements [method NetwQuantize.write], packing a decomposed [Transform3D].
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeTransform3D: Unsupported type %s." % type_string(
			typeof(value),
		),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform3D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform3D: Missing rotation.",
	)
	var transform: Transform3D = value
	origin_quantizer.write(w, transform.origin)
	var basis := transform.basis
	var rotation := basis.orthonormalized().get_rotation_quaternion()
	rotation_quantizer.write(w, rotation)
	if scale_quantizer:
		scale_quantizer.write(w, basis.get_scale())


## Implements [method NetwQuantize.read], reconstructing a [Transform3D].
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		supports_type(type),
		"NetwQuantizeTransform3D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform3D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform3D: Missing rotation.",
	)
	var origin: Vector3 = origin_quantizer.read(r, TYPE_VECTOR3)
	var rotation: Quaternion = rotation_quantizer.read(r, TYPE_QUATERNION)
	var basis := Basis(rotation)
	if scale_quantizer:
		var scale: Vector3 = scale_quantizer.read(r, TYPE_VECTOR3)
		basis = basis.scaled_local(scale)
	return Transform3D(basis, origin)


## Implements [method NetwQuantize.bit_width], summing composed widths.
func bit_width(type: Variant.Type) -> int:
	assert(
		supports_type(type),
		"NetwQuantizeTransform3D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform3D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform3D: Missing rotation.",
	)
	var total := origin_quantizer.bit_width(TYPE_VECTOR3)
	total += rotation_quantizer.bit_width(TYPE_QUATERNION)
	if scale_quantizer:
		total += scale_quantizer.bit_width(TYPE_VECTOR3)
	return total


## Implements [method NetwQuantize.max_error], summing composed error bounds.
func max_error(type: Variant.Type) -> float:
	assert(
		supports_type(type),
		"NetwQuantizeTransform3D: Unsupported type %s." % type_string(type),
	)
	assert(origin_quantizer != null, "NetwQuantizeTransform3D: Missing origin.")
	assert(
		rotation_quantizer != null,
		"NetwQuantizeTransform3D: Missing rotation.",
	)
	var total := origin_quantizer.max_error(TYPE_VECTOR3)
	total += rotation_quantizer.max_error(TYPE_QUATERNION)
	if scale_quantizer:
		total += scale_quantizer.max_error(TYPE_VECTOR3)
	return total
