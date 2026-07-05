@tool
## [NetwQuantize] that snaps a value to a fixed-point grid over a bounded
## range.
##
## A value is rounded to the nearest [member step] within
## [member min_value]..[member max_value] and packed in just enough bits to
## address the grid. [Vector3] or [Vector2] quantizes each axis the same way.
## Best for world positions (e.g. [code]position[/code] with
## [code]step = 0.5[/code]).
##
## [codeblock]
## var q := NetwQuantizeFixed.new().step(0.5).limits(-2048.0, 2048.0)
## [/codeblock]
class_name NetwQuantizeFixed
extends NetwQuantize

## Grid resolution. Smaller is more precise and uses more bits.
@export var resolution_step: float = 0.5
## Inclusive lower bound of the encoded range.
@export var min_limit: float = -2048.0
## Inclusive upper bound of the encoded range.
@export var max_limit: float = 2048.0


## Builder that sets the step.
func step(p_step: float) -> NetwQuantizeFixed:
	resolution_step = p_step
	return self


## Builder that sets the min and max limits.
func limits(p_min: float, p_max: float) -> NetwQuantizeFixed:
	min_limit = p_min
	max_limit = p_max
	return self


func _bits() -> int:
	var levels := int(ceil((max_limit - min_limit) / resolution_step)) + 1
	var b := 1
	while (1 << b) < levels:
		b += 1
	return b


func _enc(w: NetwBitBuffer.Writer, v: float) -> void:
	var bits := _bits()
	var q := int(round((clampf(v, min_limit, max_limit) - min_limit) / resolution_step))
	w.put_bits(clampi(q, 0, (1 << bits) - 1), bits)


func _dec(r: NetwBitBuffer.Reader) -> float:
	var q := r.get_bits(_bits())
	return min_limit + float(q) * resolution_step


func supports_type(type: Variant.Type) -> bool:
	return type in [TYPE_FLOAT, TYPE_INT, TYPE_VECTOR2, TYPE_VECTOR3]


## Implements [method NetwQuantize.write], snapping each [Vector3] or [Vector2]
## axis (or a scalar) to the [member resolution_step] grid before packing it.
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeFixed: Unsupported type %s." % type_string(typeof(value)),
	)
	match typeof(value):
		TYPE_VECTOR3:
			_enc(w, value.x)
			_enc(w, value.y)
			_enc(w, value.z)
		TYPE_VECTOR2:
			_enc(w, value.x)
			_enc(w, value.y)
		_:
			_enc(w, float(value))


## Implements [method NetwQuantize.read], reconstructing the value of
## [param type] from its grid index.
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		supports_type(type),
		"NetwQuantizeFixed: Unsupported type %s." % type_string(type),
	)
	match type:
		TYPE_VECTOR3:
			return Vector3(_dec(r), _dec(r), _dec(r))
		TYPE_VECTOR2:
			return Vector2(_dec(r), _dec(r))
		TYPE_INT:
			return int(round(_dec(r)))
		_:
			return _dec(r)


## Implements [method NetwQuantize.bit_width]: the grid-addressing bits per
## component, tripled for a [Vector3], or doubled for a [Vector2].
func bit_width(type: Variant.Type) -> int:
	assert(
		supports_type(type),
		"NetwQuantizeFixed: Unsupported type %s." % type_string(type),
	)
	var b := _bits()
	match type:
		TYPE_VECTOR3:
			return b * 3
		TYPE_VECTOR2:
			return b * 2
		_:
			return b


## Implements [method NetwQuantize.max_error]: half a [member resolution_step] per axis,
## combined as a magnitude for a [Vector3] or [Vector2].
func max_error(type: Variant.Type) -> float:
	assert(
		supports_type(type),
		"NetwQuantizeFixed: Unsupported type %s." % type_string(type),
	)
	var axis := resolution_step * 0.5
	match type:
		TYPE_VECTOR3:
			return axis * sqrt(3.0)
		TYPE_VECTOR2:
			return axis * sqrt(2.0)
		_:
			return axis
