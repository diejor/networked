## A list of labelled form fields, built from a settings [Dictionary] or a
## join schema [Array].
class_name ConnectFieldList
extends RefCounted

const LABEL_WIDTH := 90.0

var _keys: Array[StringName] = []
var _controls: Array[Control] = []
var _types: Array[int] = []


## Replaces the contents of [param container] with one field per entry of
## [param defaults]. Entries that cannot be edited in a field are skipped.
func render_settings(container: Container, defaults: Dictionary) -> void:
	_clear(container)
	for key: Variant in defaults.keys():
		var default_value: Variant = defaults[key]
		if not ConnectBrowser.can_author_value(default_value):
			continue
		var control := ConnectBrowser.make_value_control(
			default_value,
			StringName(key),
		)
		_add(container, String(key).capitalize(), control)
		_keys.append(StringName(key))
		_types.append(typeof(default_value))


## Replaces the contents of [param container] with one empty field per entry
## of [param schema], in order.
func render_schema(container: Container, schema: Array) -> void:
	_clear(container)
	for entry: Dictionary in schema:
		var value_type := int(entry.get("type", TYPE_STRING))
		var control := ConnectBrowser.make_value_control(
			ConnectBrowser.zero_value(value_type),
		)
		_add(container, String(entry.get("name", "")).capitalize(), control)
		_keys.append(StringName(entry.get("name", "")))
		_types.append(value_type)


## Returns [code]true[/code] when there are no fields.
func is_empty() -> bool:
	return _controls.is_empty()


## Returns the field values, keyed by name.
func as_dictionary() -> Dictionary:
	var values: Dictionary = { }
	for at in _controls.size():
		values[_keys[at]] = ConnectBrowser.value_from_control(
			_controls[at],
			_types[at],
		)
	return values


## Returns the field values in order, as join arguments.
func as_array() -> Array:
	var values: Array = []
	for at in _controls.size():
		values.append(
			ConnectBrowser.value_from_control(
				_controls[at],
				_types[at],
			),
		)
	return values


func _clear(container: Container) -> void:
	for child in container.get_children():
		child.queue_free()
	_keys.clear()
	_controls.clear()
	_types.clear()


func _add(container: Container, title: String, control: Control) -> void:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.custom_minimum_size = Vector2(LABEL_WIDTH, 0)
	label.text = title
	label.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	row.add_child(label)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	container.add_child(row)
	_controls.append(control)
