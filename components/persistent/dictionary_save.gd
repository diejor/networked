@tool
## Resource that stores arbitrary key/value pairs in a Dictionary.
class_name DictionarySave
extends SaveContainer

@export var data: Dictionary[StringName, Variant] = {}


func serialize() -> PackedByteArray:
	return var_to_bytes(data)

func deserialize(bytes: PackedByteArray) -> void:
	data = bytes_to_var(bytes)


func set_value(property: StringName, value: Variant) -> void:
	data[property] = value

func get_value(property: StringName, default: Variant = null) -> Variant:
	return data.get(property, default)

func has_value(property: StringName) -> bool:
	return data.has(property)


func get_property_names() -> Array[StringName]:
	return data.keys()
