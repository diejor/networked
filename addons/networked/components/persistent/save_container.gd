## Abstract key/value store that backs a [SaveSynchronizer] and supports serialization.
##
## Subclass this (e.g. [DictionarySave]) and override the four abstract methods to provide
## a concrete storage strategy. Implements GDScript's iterator protocol so containers can
## be iterated with a [code]for key in container[/code] loop.
@tool
@abstract
class_name SaveContainer
extends Serde

## Stores [param value] under [param property].
@abstract func set_value(property: StringName, value: Variant) -> void

## Returns the value stored under [param property], or [param default] if absent.
@abstract func get_value(property: StringName, default: Variant = null) -> Variant

## Returns [code]true[/code] if [param property] has been stored in this container.
@abstract func has_value(property: StringName) -> bool

## Returns all property names currently stored in this resource.
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
