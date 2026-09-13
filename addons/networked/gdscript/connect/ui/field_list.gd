## One rendered list of labelled fields, and the values read back out of it.
##
## A form hands it a container and either a settings [Dictionary] or a join
## schema [Array], and reads the rows back as whichever shape it needs.
class_name ConnectFieldList
extends RefCounted

const LABEL_WIDTH := 90.0

var _keys: Array[StringName] = []
var _controls: Array[Control] = []
var _types: Array[int] = []


## Draws one row per authorable entry of [param defaults] into [param
## container], replacing whatever it held. An entry whose default no control
## can carry is skipped, so an installation seam never becomes a field.
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


## Draws one row per entry of [param schema] into [param container], in the
## order the schema lists them, seeded with the empty value of each type.
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


## Whether anything was drawn, which is what decides if a settings section is
## worth showing at all.
func is_empty() -> bool:
	return _controls.is_empty()


## The rows read back as a [Dictionary] keyed the way they were rendered.
func as_dictionary() -> Dictionary:
	var values: Dictionary = { }
	for at in _controls.size():
		values[_keys[at]] = ConnectBrowser.value_from_control(
			_controls[at],
			_types[at],
		)
	return values


## The rows read back positionally, which is the shape a join schema takes.
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
