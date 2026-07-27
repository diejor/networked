@tool
## [NetwQuantize] that wraps an angle in radians onto an exact bit count.
##
## The value is taken modulo [constant @GDScript.TAU] and mapped to
## [member bit_count] bits, so every angle has a code and the wrap costs
## nothing: two peers a hair either side of the boundary encode to neighbouring
## codes rather than to opposite ends of a range. Applies to a [float] angle.
##
## Which half-open range it decodes into is a declaration, because the field it
## writes back into has one. A Godot Euler component carries
## [code]-PI < angle <= PI[/code], so restoring [code]0 <= angle < TAU[/code]
## into it would leave the game reading a value outside the range it stores
## angles in, even though the two name the same rotation.
## [codeblock]
## var q := NetwQuantizeAngle.new().bits(8)              # 0 <= angle < TAU
## var e := NetwQuantizeAngle.new().bits(16).centered()  # -PI < angle <= PI
## [/codeblock]
class_name NetwQuantizeAngle
extends NetwQuantize

## Bits for the angle.
@export_range(1, 32, 1) var bit_count: int = 8
## Whether the decoded range is centered on zero
## ([code]-PI < angle <= PI[/code]) rather than starting there
## ([code]0 <= angle < TAU[/code]). The encoding is identical either way.
@export var centered_on_zero: bool = false


## Builder that sets the bit count.
func bits(p_bits: int) -> NetwQuantizeAngle:
	bit_count = p_bits
	return self


## Builder that centers the decoded range on zero, matching the convention a
## Godot Euler component stores its angles in.
func centered() -> NetwQuantizeAngle:
	centered_on_zero = true
	return self


func _supports_type(type: Variant.Type) -> bool:
	return type in [TYPE_FLOAT, TYPE_INT]


## Implements [method NetwQuantize._write], wrapping the angle from 0
## to [constant @GDScript.TAU] before packing it into [member bit_count] bits.
func _write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		_supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeAngle: Unsupported type %s." % type_string(typeof(value)),
	)
	var levels := 1 << bit_count
	var f := fposmod(float(value), TAU) / TAU
	w.put_bits(int(round(f * levels)) % levels, bit_count)


## Implements [method NetwQuantize._read], decoding the angle into the range
## [member centered_on_zero] selects. The angle is scalar, so [param type] must
## be scalar.
func _read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		_supports_type(type),
		"NetwQuantizeAngle: Unsupported type %s." % type_string(type),
	)
	var q := r.get_bits(bit_count)
	var angle := (float(q) / float(1 << bit_count)) * TAU
	# Exactly PI stays positive, so the centered range is half-open the same way
	# a Godot Euler component is and the two agree on the boundary code.
	if centered_on_zero and angle > PI:
		angle -= TAU
	return int(round(angle)) if type == TYPE_INT else angle


## Implements [method NetwQuantize._bit_width]: always [member bit_count],
## independent of [param type].
func _bit_width(type: Variant.Type) -> int:
	assert(
		_supports_type(type),
		"NetwQuantizeAngle: Unsupported type %s." % type_string(type),
	)
	return bit_count


## Implements [method NetwQuantize._max_error]: half the angular resolution
## ([code]TAU / 2^bits[/code]) in radians.
func _max_error(type: Variant.Type) -> float:
	assert(
		_supports_type(type),
		"NetwQuantizeAngle: Unsupported type %s." % type_string(type),
	)
	return TAU / float(1 << bit_count) * 0.5
