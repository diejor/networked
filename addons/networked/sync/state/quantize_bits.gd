@tool
## [NetwQuantize] that maps a bounded range onto an exact bit count per component.
##
## A value in [member min_value]..[member max_value] is linearly mapped to
## [member bits] bits. [Vector2] quantizes each axis with the same range. Best for
## normalized directions and bounded scalars (bomber [code]motion[/code] is
## [code]bits = 8, [-1, 1][/code]).
##
## The grid uses [code]2^bits[/code] codes anchored at [member min_value], so the
## center of a symmetric range round-trips exactly. A value at rest
## ([code]0[/code] for [code][-x, x][/code]) decodes back to [code]0[/code], which
## matters when game logic compares an axis to exactly zero. The top endpoint
## resolves within one step instead of exactly.
##
## [codeblock]
## var q := NetwQuantizeBits.new()
## q.bits = 8
## q.min_value = -1.0
## q.max_value = 1.0
## [/codeblock]
class_name NetwQuantizeBits
extends NetwQuantize

## Bits per component.
@export_range(1, 32, 1) var bits: int = 8
## Inclusive lower bound of the encoded range.
@export var min_value: float = -1.0
## Inclusive upper bound of the encoded range.
@export var max_value: float = 1.0


# Number of distinct codes. A 2^bits divisor (mid-rise) keeps the range center on
# the grid, unlike a 2^bits-1 divisor which straddles it.
func _codes() -> int:
	return 1 << bits


func _enc(w: NetwBitBuffer.Writer, v: float) -> void:
	var span := max_value - min_value
	var codes := _codes()
	var f := 0.0 if span == 0.0 else clampf((v - min_value) / span, 0.0, 1.0)
	w.put_bits(clampi(int(round(f * codes)), 0, codes - 1), bits)


func _dec(r: NetwBitBuffer.Reader) -> float:
	var f := float(r.get_bits(bits)) / float(_codes())
	return min_value + f * (max_value - min_value)


## Implements [method NetwQuantize.write], packing each [Vector2] axis (or a
## scalar) into [member bits] bits across [member min_value]..[member max_value].
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	if typeof(value) == TYPE_VECTOR2:
		_enc(w, value.x)
		_enc(w, value.y)
	else:
		_enc(w, float(value))


## Implements [method NetwQuantize.read], reconstructing the value of [param type]
## from [member bits] bits per component.
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	match type:
		TYPE_VECTOR2:
			return Vector2(_dec(r), _dec(r))
		TYPE_INT:
			return int(round(_dec(r)))
		_:
			return _dec(r)


## Implements [method NetwQuantize.bit_width]: [member bits] per component, doubled
## for a [Vector2].
func bit_width(type: Variant.Type) -> int:
	return bits * (2 if type == TYPE_VECTOR2 else 1)


## Implements [method NetwQuantize.max_error]: half the grid spacing
## ([code]span / 2^bits[/code]) per axis, combined as a magnitude for a [Vector2].
func max_error(type: Variant.Type) -> float:
	var axis := (max_value - min_value) / float(1 << bits) * 0.5
	return axis * sqrt(2.0) if type == TYPE_VECTOR2 else axis
