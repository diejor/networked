## [NetwRecord] backed by a plain [Dictionary].
##
## [DictionaryRecord] is the default mutable record implementation. It stores
## arbitrary [Variant] values keyed by [StringName] and serializes them with
## [method @GlobalScope.var_to_bytes].
##
## [codeblock]
## var record := DictionaryRecord.new()
## record.set_value(&"health", 100)
## record.position = Vector2(10, 20)
##
## var bytes := record.serialize()
## var copy := DictionaryRecord.new()
## copy.deserialize(bytes)
## [/codeblock]
@tool
class_name DictionaryRecord
extends NetwRecord

## The backing dictionary serialized to disk and transmitted over the network.
@export var data: Dictionary[StringName, Variant] = { }


func _init() -> void:
	resource_local_to_scene = true
	if data == null:
		data = { }


## Serializes [member data] with [method @GlobalScope.var_to_bytes].
func serialize() -> PackedByteArray:
	return var_to_bytes(data)


## Repopulates [member data] from [param bytes].
func deserialize(bytes: PackedByteArray) -> void:
	data = bytes_to_var(bytes)


## Stores [param value] under [param property] in [member data].
func set_value(property: StringName, value: Variant) -> void:
	data[property] = value


## Returns the value for [param property], or [param default] if absent.
func get_value(property: StringName, default: Variant = null) -> Variant:
	return data.get(property, default)


## Returns [code]true[/code] if [param property] exists in [member data].
func has_value(property: StringName) -> bool:
	return data.has(property)


## Returns all keys currently stored in [member data].
func get_property_names() -> Array[StringName]:
	return data.keys()
