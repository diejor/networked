@tool
## [NetwQuantize] that maps a bounded range onto an exact bit count per
## component.
##
## A value in [member min_value]..[member max_value] is linearly mapped to
## [member bits] bits. [Vector3] or [Vector2] quantizes each axis with the
## same range. Best for normalized directions and bounded scalars (e.g.
## [code]motion[/code] with [code]bits = 8, [-1, 1][/code]).
##
## The grid uses [code]2^bits[/code] codes anchored at [member min_value], so
## the center of a symmetric range round-trips exactly. A value at rest
## ([code]0[/code] for a symmetric range) decodes back to [code]0[/code],
## which matters when game logic compares an axis to exactly zero. The top
## endpoint resolves within one step instead of exactly.
##
## [codeblock]
## var q := NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0)
## [/codeblock]
class_name NetwQuantizeBits
extends NetwQuantize

## Bits per component.
@export_range(1, 32, 1) var bit_count: int = 8
## Inclusive lower bound of the encoded range.
@export var min_limit: float = -1.0
## Inclusive upper bound of the encoded range.
@export var max_limit: float = 1.0


## Builder that sets the bit count.
func bits(p_bits: int) -> NetwQuantizeBits:
	bit_count = p_bits
	return self


## Builder that sets the min and max limits.
func limits(p_min: float, p_max: float) -> NetwQuantizeBits:
	min_limit = p_min
	max_limit = p_max
	return self


# Number of distinct codes. A 2^bits divisor keeps the range center on the grid.
# A 2^bits minus 1 divisor straddles it.
func _codes() -> int:
	return 1 << bit_count


func _enc(w: NetwBitBuffer.Writer, v: float) -> void:
	var span := max_limit - min_limit
	var codes := _codes()
	var f := 0.0 if span == 0.0 else clampf((v - min_limit) / span, 0.0, 1.0)
	w.put_bits(clampi(int(round(f * codes)), 0, codes - 1), bit_count)


func _dec(r: NetwBitBuffer.Reader) -> float:
	var f := float(r.get_bits(bit_count)) / float(_codes())
	return min_limit + f * (max_limit - min_limit)


## Implements [method NetwQuantize.supports_type].
func supports_type(type: Variant.Type) -> bool:
	return type in [TYPE_FLOAT, TYPE_INT, TYPE_VECTOR2, TYPE_VECTOR3]


## Implements [method NetwQuantize.write], packing each [Vector3] or [Vector2]
## axis (or a scalar) into [member bit_count] bits across
## [member min_limit]..[member max_limit].
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(typeof(value)),
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
## [param type] from [member bit_count] bits per component.
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		supports_type(type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(type),
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


## Implements [method NetwQuantize.bit_width]: [member bit_count] per component,
## tripled for a [Vector3], or doubled for a [Vector2].
func bit_width(type: Variant.Type) -> int:
	assert(
		supports_type(type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(type),
	)
	match type:
		TYPE_VECTOR3:
			return bit_count * 3
		TYPE_VECTOR2:
			return bit_count * 2
		_:
			return bit_count


## Implements [method NetwQuantize.max_error]: half the grid spacing
## ([code]span / 2^bits[/code]) per axis, combined as a magnitude for a
## [Vector3] or [Vector2].
func max_error(type: Variant.Type) -> float:
	assert(
		supports_type(type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(type),
	)
	var axis := (max_limit - min_limit) / float(1 << bit_count) * 0.5
	match type:
		TYPE_VECTOR3:
			return axis * sqrt(3.0)
		TYPE_VECTOR2:
			return axis * sqrt(2.0)
		_:
			return axis
