## Abstract key-value record for serializable networked data.
##
## [NetwRecord] is the shared value object behind [SaveComponent] persistence,
## [NetwDatabase] rows, and detached state samples. It stores named values
## without owning the scene object those values came from.
##
## [codeblock]
## var row: NetwRecord = db.table(&"players").fetch(username)
## %SaveComponent.record = row
##
## row.set_value(&"health", 75)
## var hp: int = row.get_value(&"health", 100)
##
## for key in row:
##     print(key, " = ", row.get_value(key))
## [/codeblock]
@tool
@abstract
class_name NetwRecord
extends Serde

## Stores [param value] under [param property].
@abstract func set_value(property: StringName, value: Variant) -> void


## Returns the value stored under [param property], or [param default] if absent.
@abstract func get_value(property: StringName, default: Variant = null) -> Variant


## Returns [code]true[/code] if [param property] is present.
@abstract func has_value(property: StringName) -> bool


## Returns the property names stored in this record.
@abstract func get_property_names() -> Array[StringName]


## Returns [code]true[/code] when no values are stored.
func is_empty() -> bool:
	return get_property_names().is_empty()


## Returns this record's data as a plain [Dictionary].
##
## The default implementation iterates [method get_property_names] and calls
## [method get_value] for each key. Override it to remap stored names.
##
## [codeblock]
## func to_dict() -> Dictionary:
##     return {&"hp": get_value(&"health")}
## [/codeblock]
func to_dict() -> Dictionary:
	var dict: Dictionary = { }
	for key: StringName in get_property_names():
		dict[key] = get_value(key)
	return dict


## Populates this record from [param data].
##
## The default implementation calls [method set_value] for every key in
## [param data]. Override it to coerce types or migrate stored names.
##
## [codeblock]
## func from_dict(data: Dictionary) -> void:
##     set_value(&"health", data.get(&"hp", 100))
## [/codeblock]
func from_dict(data: Dictionary) -> void:
	for key: StringName in data:
		set_value(key, data[key])


func _get(property: StringName) -> Variant:
	if has_value(property):
		return get_value(property)
	return null


func _set(property: StringName, value: Variant) -> bool:
	set_value(property, value)
	return true


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	for property: StringName in get_property_names():
		props.append({ "name": property, "type": typeof(get_value(property)) })
	return props


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
