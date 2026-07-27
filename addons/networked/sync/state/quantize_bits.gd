@tool
## [NetwQuantize] that maps a bounded range onto an exact bit count per
## component.
##
## A value in [member min_value]..[member max_value] is linearly mapped to
## [member bits] bits. [Vector3] or [Vector2] quantizes each axis with the
## same range. Best for normalized directions and bounded scalars (e.g.
## [code]motion[/code] with [code]bits = 8, [-1, 1][/code]).
##
## The grid spans the declared range inclusively and holds an odd number of
## levels, so three values a game depends on all survive the wire: both
## declared limits, and the rest point of a symmetric range.
##
## Those three cannot coexist on a [code]2^bits[/code] grid. Spanning
## [member min_limit] to [member max_limit] inclusively over [code]N[/code]
## levels puts the midpoint on a level only when [code]N[/code] is odd, and
## [code]2^bits[/code] is even. The grid therefore uses
## [code]2^bits - 1[/code] levels and leaves one bit pattern unused, which
## costs under one percent of resolution at eight bits and nothing measurable
## above sixteen.
## [codeblock]
## var q := NetwQuantizeBits.new().bits(8).limits(-1.0, 1.0)
##
## bits(4).limits(-1, 1)   15 levels, step 2/14
##   -1.0  ──> -1.0    the declared floor
##    0.0  ──>  0.0    a stick at neutral, a body at rest
##   +1.0  ──> +1.0    the declared ceiling
## [/codeblock]
## A quantizer whose endpoints do not round-trip is invisible to every
## comparison, because both peers encode the same code, while the two games
## read values a whole step apart. Nothing downstream can attribute that,
## which is why exactness here is a wire property rather than a tuning
## preference.
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


# Levels on the grid, held odd so a symmetric range's midpoint is one of them.
# One bit pattern goes unused to buy that. A single-bit field cannot hold three
# distinct values at all, so it degenerates to the two endpoints.
func _levels() -> int:
	return maxi(2, (1 << bit_count) - 1)


func _enc(w: NetwBitBuffer.Writer, v: float) -> void:
	var span := max_limit - min_limit
	var top := _levels() - 1
	var f := 0.0 if span == 0.0 else clampf((v - min_limit) / span, 0.0, 1.0)
	w.put_bits(clampi(int(round(f * top)), 0, top), bit_count)


func _dec(r: NetwBitBuffer.Reader) -> float:
	# The unused pattern decodes to the ceiling rather than past it. A frame
	# this peer did not write must never produce a value outside the range the
	# declaration promised its readers.
	var top := _levels() - 1
	var f := float(mini(r.get_bits(bit_count), top)) / float(top)
	return min_limit + f * (max_limit - min_limit)


## Implements [method NetwQuantize._supports_type].
func _supports_type(type: Variant.Type) -> bool:
	return type in [TYPE_FLOAT, TYPE_INT, TYPE_VECTOR2, TYPE_VECTOR3]


## Implements [method NetwQuantize._write], packing each [Vector3] or [Vector2]
## axis (or a scalar) into [member bit_count] bits across
## [member min_limit]..[member max_limit].
func _write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		_supports_type(typeof(value) as Variant.Type),
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


## Implements [method NetwQuantize._read], reconstructing the value of
## [param type] from [member bit_count] bits per component.
func _read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		_supports_type(type),
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


## Implements [method NetwQuantize._bit_width]: [member bit_count] per component,
## tripled for a [Vector3], or doubled for a [Vector2].
func _bit_width(type: Variant.Type) -> int:
	assert(
		_supports_type(type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(type),
	)
	match type:
		TYPE_VECTOR3:
			return bit_count * 3
		TYPE_VECTOR2:
			return bit_count * 2
		_:
			return bit_count


## Implements [method NetwQuantize._max_error]: half the grid spacing
## ([code]span / (2^bits - 2)[/code]) per axis, combined as a magnitude for a
## [Vector3] or [Vector2].
func _max_error(type: Variant.Type) -> float:
	assert(
		_supports_type(type),
		"NetwQuantizeBits: Unsupported type %s." % type_string(type),
	)
	var axis := (max_limit - min_limit) / float(_levels() - 1) * 0.5
	match type:
		TYPE_VECTOR3:
			return axis * sqrt(3.0)
		TYPE_VECTOR2:
			return axis * sqrt(2.0)
		_:
			return axis
