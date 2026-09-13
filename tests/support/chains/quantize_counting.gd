extends NetwQuantize

var encodes := 0
var decodes := 0


func _supports_type(type: int) -> bool:
	return type == TYPE_VECTOR2


func _bit_width(_type: int) -> int:
	return 10


func _stride(_type: int) -> int:
	return 2


func _encode(value: Variant, element: int) -> int:
	encodes += 1
	return roundi((value as Vector2)[element]) & 0x3ff


func _decode(codes: PackedInt64Array, _type: int) -> Variant:
	decodes += 1
	return Vector2(float(codes[0]), float(codes[1]))
