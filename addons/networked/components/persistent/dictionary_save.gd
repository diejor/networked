## [Entity] backed by a plain [Dictionary].
##
## The go-to choice for rapid prototyping and schema-less data. Stores arbitrary
## [Variant] values keyed by [StringName] and round-trips them as a
## [PackedByteArray] via [code]var_to_bytes[/code] / [code]bytes_to_var[/code],
## preserving all Godot types (e.g. [Vector2], [Color]).
##
## Files can be saved to disk as [code].tres[/code], [code].dict[/code] (binary),
## or [code].tdict[/code] (JSON) via the custom format loaders bundled with this addon.
##
## [codeblock lang=gdscript]
## var entity := DictionaryEntity.new()
## entity.set_value(&"health",   100)
## entity.set_value(&"position", Vector2(10, 20))
##
## # Binary round-trip (network / storage):
## var bytes := entity.serialize()
## var copy  := DictionaryEntity.new()
## copy.deserialize(bytes)
##
## # Database round-trip:
## var dict := entity.to_dict()
## entity.from_dict(dict)
## [/codeblock]
@tool
class_name DictionaryEntity
extends Entity

## The backing dictionary serialized to disk and transmitted over the network.
@export var data: Dictionary[StringName, Variant] = {}

func _init() -> void:
	resource_local_to_scene = true
	if data == null:
		data = {}


# ── Network / Storage interface ───────────────────────────────────────────────

## Serializes [member data] to a [PackedByteArray] using [code]var_to_bytes[/code].
func serialize() -> PackedByteArray:
	return var_to_bytes(data)

## Repopulates [member data] from a [PackedByteArray] produced by [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	data = bytes_to_var(bytes)


# ── Key / value API ───────────────────────────────────────────────────────────

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
