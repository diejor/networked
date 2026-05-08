## Abstract data model for the persistence layer.
##
## [method to_dict] / [method from_dict] map to the database schema.
## [method serialize] / [method deserialize] round-trip through a
## [PackedByteArray] for network transport.
##
## Assign a concrete subclass (e.g. [DictionaryEntity]) to
## [member SaveComponent.bound_entity].
##
## [codeblock]
## var entity: Entity = db.table(&"players").fetch(username)
## save_comp.bound_entity = entity
##
## entity.set_value(&"health", 75)
## var hp: int = entity.get_value(&"health", 100)
##
## for key in entity:
##     print(key, " = ", entity.get_value(key))
## [/codeblock]
@tool
@abstract
class_name Entity
extends Serde

# ── Key / value API ───────────────────────────────────────────────────────────

## Stores [param value] under [param property].
@abstract func set_value(property: StringName, value: Variant) -> void

## Returns the value stored under [param property], or [param default] if absent.
@abstract func get_value(property: StringName, default: Variant = null) -> Variant

## Returns [code]true[/code] if [param property] has been stored in this entity.
@abstract func has_value(property: StringName) -> bool

## Returns all property names currently stored in this entity.
@abstract func get_property_names() -> Array[StringName]


# ── Database interface ────────────────────────────────────────────────────────

## Returns this entity's data as a plain [Dictionary] for database storage.
##
## The default implementation iterates [method get_property_names] and calls
## [method get_value] for each key. Override to control exactly which fields
## are persisted or to rename columns:
## [codeblock lang=gdscript]
## func to_dict() -> Dictionary:
##     return {&"hp": get_value(&"health"), &"mp": get_value(&"mana")}
## [/codeblock]
func to_dict() -> Dictionary:
	var dict: Dictionary = {}
	for key: StringName in get_property_names():
		dict[key] = get_value(key)
	return dict


## Populates this entity from a [Dictionary] returned by the database.
##
## The default implementation calls [method set_value] for every key in [param data].
## Override to perform type coercion, migration logic, or key remapping:
## [codeblock lang=gdscript]
## func from_dict(data: Dictionary) -> void:
##     set_value(&"health", data.get(&"hp", 100))
##     set_value(&"mana",   data.get(&"mp", 50))
## [/codeblock]
func from_dict(data: Dictionary) -> void:
	for key: StringName in data:
		set_value(key, data[key])


# ── Iterator protocol ─────────────────────────────────────────────────────────

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
