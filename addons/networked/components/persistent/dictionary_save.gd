## [SaveContainer] implementation backed by a plain [Dictionary].
##
## Stores arbitrary [Variant] values keyed by [StringName]. Can be saved to disk as a
## [code].tres[/code] resource or as a custom [code].dict[/code] / [code].tdict[/code]
## file via [DictionarySaveFormatLoader] and [DictionarySaveFormatSaver].
@tool
class_name DictionarySave
extends SaveContainer

## The backing dictionary serialized to disk and transmitted over the network.
@export var data: Dictionary[StringName, Variant] = {}

func _init() -> void:
	resource_local_to_scene = true
	if data == null:
		data = {}


## Serializes [member data] to a [PackedByteArray] using [code]var_to_bytes[/code].
func serialize() -> PackedByteArray:
	return var_to_bytes(data)

## Repopulates [member data] from a [PackedByteArray] produced by [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	data = bytes_to_var(bytes)


## Stores [param value] under [param property] in [member data].
func set_value(property: StringName, value: Variant) -> void:
	data[property] = value

## Returns the value for [param property], or [param default] if the key is absent.
func get_value(property: StringName, default: Variant = null) -> Variant:
	return data.get(property, default)

## Returns [code]true[/code] if [param property] exists in [member data].
func has_value(property: StringName) -> bool:
	return data.has(property)


## Returns all keys currently stored in [member data].
func get_property_names() -> Array[StringName]:
	return data.keys()
