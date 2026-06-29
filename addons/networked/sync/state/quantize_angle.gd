@tool
## [NetwQuantize] that wraps an angle in radians onto an exact bit count.
##
## The value is taken modulo [constant @GDScript.TAU] and mapped to [member bits]
## bits, so it always decodes back into [code][0, TAU)[/code] with no boundary
## discontinuity. Applies to a [float] angle (a node [code]rotation[/code]).
##
## [codeblock]
## var q := NetwQuantizeAngle.new()
## q.bits = 8   # ~1.4 degree resolution
## [/codeblock]
class_name NetwQuantizeAngle
extends NetwQuantize

## Bits for the angle.
@export_range(1, 32, 1) var bits: int = 8


## Implements [method NetwQuantize.write], wrapping the angle from 0
## to [constant @GDScript.TAU] before packing it into [member bits] bits.
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	var levels := 1 << bits
	var f := fposmod(float(value), TAU) / TAU
	w.put_bits(int(round(f * levels)) % levels, bits)


## Implements [method NetwQuantize.read], decoding the angle back
## from 0 to [constant @GDScript.TAU]. The angle is scalar, so
## [param _type] is unused.
func read(r: NetwBitBuffer.Reader, _type: Variant.Type) -> Variant:
	var q := r.get_bits(bits)
	return (float(q) / float(1 << bits)) * TAU


## Implements [method NetwQuantize.bit_width]: always [member bits], independent of
## [param _type].
func bit_width(_type: Variant.Type) -> int:
	return bits


## Implements [method NetwQuantize.max_error]: half the angular resolution
## ([code]TAU / 2^bits[/code]) in radians. The angle is scalar, so [param _type] is
## unused.
func max_error(_type: Variant.Type) -> float:
	return TAU / float(1 << bits) * 0.5
