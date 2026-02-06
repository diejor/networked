@tool
@abstract
class_name SaveContainer
extends Resource


@abstract func serialize() -> PackedByteArray
@abstract func deserialize(bytes: PackedByteArray) -> void

@abstract func set_value(property: StringName, value: Variant) -> void
@abstract func get_value(property: StringName, default: Variant = null) -> Variant
@abstract func has_value(property: StringName) -> bool

## All property names stored in this resource.
@abstract func get_property_names() -> Array[StringName]


var _iter_keys: Array[StringName] = []
var _iter_index: int = 0

func _iter_init(_arg: Variant) -> bool:
	_iter_keys = get_property_names()
	_iter_index = 0
	return _iter_keys.size() > 0


func _iter_next(_arg: Variant) -> bool:
	_iter_index += 1
	return _iter_index < _iter_keys.size()


func _iter_get(_arg: Variant) -> StringName:
	return _iter_keys[_iter_index]
