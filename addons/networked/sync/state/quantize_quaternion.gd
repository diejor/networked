@tool
## [NetwQuantize] that compresses a unit [Quaternion] with smallest-three
## encoding.
##
## The largest component is omitted and reconstructed from unit length. The
## other three components are quantized across
## [code]-1 / sqrt(2) <= component <= 1 / sqrt(2)[/code].
## This stores any rotation in
## [code]2 + bits * 3[/code] bits.
##
## [codeblock]
## var q := NetwQuantizeQuaternion.new().bits(10)
## [/codeblock]
class_name NetwQuantizeQuaternion
extends NetwQuantize

const _COMPONENT_LIMIT := 0.7071067811865476

## Bits per stored quaternion component.
@export_range(1, 20, 1) var bit_count: int = 10


## Builder that sets the bit count.
func bits(p_bits: int) -> NetwQuantizeQuaternion:
	bit_count = p_bits
	return self


func _supports_type(type: Variant.Type) -> bool:
	return type == TYPE_QUATERNION


func _component_count() -> int:
	return 1 << bit_count


func _enc_component(w: NetwBitBuffer.Writer, v: float) -> void:
	var max_code := _component_count() - 1
	var span := _COMPONENT_LIMIT * 2.0
	var f := clampf((v + _COMPONENT_LIMIT) / span, 0.0, 1.0)
	w.put_bits(clampi(int(round(f * max_code)), 0, max_code), bit_count)


func _dec_component(r: NetwBitBuffer.Reader) -> float:
	var max_code := _component_count() - 1
	var f := float(r.get_bits(bit_count)) / float(max_code)
	return (f * _COMPONENT_LIMIT * 2.0) - _COMPONENT_LIMIT


func _components(q: Quaternion) -> Array[float]:
	return [q.x, q.y, q.z, q.w]


## Implements [method NetwQuantize._write], packing a normalized [Quaternion].
func _write(w: NetwBitBuffer.Writer, value: Variant) -> void:
	assert(
		_supports_type(typeof(value) as Variant.Type),
		"NetwQuantizeQuaternion: Unsupported type %s." % type_string(
			typeof(value),
		),
	)
	var q: Quaternion = (value as Quaternion).normalized()
	var components := _components(q)
	var largest := 0
	for i in range(1, 4):
		if absf(components[i]) > absf(components[largest]):
			largest = i
	if components[largest] < 0.0:
		for i in 4:
			components[i] = -components[i]
	w.put_bits(largest, 2)
	for i in 4:
		if i != largest:
			_enc_component(w, components[i])


## Implements [method NetwQuantize._read], reconstructing a [Quaternion].
func _read(r: NetwBitBuffer.Reader, type: Variant.Type) -> Variant:
	assert(
		_supports_type(type),
		"NetwQuantizeQuaternion: Unsupported type %s." % type_string(type),
	)
	var largest := r.get_bits(2)
	var components: Array[float] = [0.0, 0.0, 0.0, 0.0]
	var length_sq := 0.0
	for i in 4:
		if i == largest:
			continue
		var component := _dec_component(r)
		components[i] = component
		length_sq += component * component
	components[largest] = sqrt(maxf(0.0, 1.0 - length_sq))
	return Quaternion(
		components[0],
		components[1],
		components[2],
		components[3],
	).normalized()


## Implements [method NetwQuantize._bit_width]: two index bits plus three
## quantized components.
func _bit_width(type: Variant.Type) -> int:
	assert(
		_supports_type(type),
		"NetwQuantizeQuaternion: Unsupported type %s." % type_string(type),
	)
	return 2 + bit_count * 3


## Implements [method NetwQuantize._max_error]: a conservative angular error in
## radians after reconstructing the omitted component.
func _max_error(type: Variant.Type) -> float:
	assert(
		_supports_type(type),
		"NetwQuantizeQuaternion: Unsupported type %s." % type_string(type),
	)
	var axis := _COMPONENT_LIMIT / float(_component_count() - 1)
	return 2.0 * asin(minf(1.0, axis * sqrt(3.0)))
