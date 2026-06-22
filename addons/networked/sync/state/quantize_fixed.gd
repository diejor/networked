@tool
## [NetwQuantize] that snaps a value to a fixed-point grid over a bounded range.
##
## A value is rounded to the nearest [member step] within
## [member min_value]..[member max_value] and packed in just enough bits to address
## the grid. [Vector2] quantizes each axis the same way. Best for world positions
## (bomber [code]position[/code] is [code]step = 0.5[/code] over the arena).
##
## [codeblock]
## var q := NetwQuantizeFixed.new()
## q.step = 0.5
## q.min_value = -2048.0
## q.max_value = 2048.0
## [/codeblock]
class_name NetwQuantizeFixed
extends NetwQuantize

## Grid resolution. Smaller is more precise and uses more bits.
@export var step: float = 0.5
## Inclusive lower bound of the encoded range.
@export var min_value: float = -2048.0
## Inclusive upper bound of the encoded range.
@export var max_value: float = 2048.0


func _bits() -> int:
	var levels := int(ceil((max_value - min_value) / step)) + 1
	var b := 1
	while (1 << b) < levels:
		b += 1
	return b


func _enc(w: NetwBitBuffer.Writer, v: float) -> void:
	var bits := _bits()
	var q := int(round((clampf(v, min_value, max_value) - min_value) / step))
	w.put_bits(clampi(q, 0, (1 << bits) - 1), bits)


func _dec(r: NetwBitBuffer.Reader) -> float:
	var q := r.get_bits(_bits())
	return min_value + float(q) * step


## Implements [method NetwQuantize.write], snapping each [Vector2] axis (or a
## scalar) to the [member step] grid before packing it.
func write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	if typeof(value) == TYPE_VECTOR2:
		_enc(w, value.x)
		_enc(w, value.y)
	else:
		_enc(w, float(value))


## Implements [method NetwQuantize.read], reconstructing the value of [param type]
## from its grid index.
func read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	match type:
		TYPE_VECTOR2:
			return Vector2(_dec(r), _dec(r))
		TYPE_INT:
			return int(round(_dec(r)))
		_:
			return _dec(r)


## Implements [method NetwQuantize.bit_width]: the grid-addressing bits per
## component, doubled for a [Vector2].
func bit_width(type: Variant.Type) -> int:
	return _bits() * (2 if type == TYPE_VECTOR2 else 1)


## Implements [method NetwQuantize.max_error]: half a [member step] per axis,
## combined as a magnitude for a [Vector2].
func max_error(type: Variant.Type) -> float:
	var axis := step * 0.5
	return axis * sqrt(2.0) if type == TYPE_VECTOR2 else axis
