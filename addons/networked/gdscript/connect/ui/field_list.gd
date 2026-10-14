## Labelled form inputs built from settings or connection requirements.
class_name ConnectFieldList
extends RefCounted

const LABEL_WIDTH := 90.0
const STRING_LIST_EDIT := preload(
	"res://addons/networked/gdscript/connect/ui/string_list_edit.tscn"
)
const ICE_SERVERS_EDIT := preload(
	"res://addons/networked/gdscript/connect/ui/ice_servers_edit.tscn"
)

var keys: Array[StringName] = []
var controls: Array[Control] = []
var types: Array[int] = []


func render_settings(container: Container, settings: Dictionary) -> void:
	for child in container.get_children():
		child.queue_free()
	keys.clear()
	controls.clear()
	types.clear()
	for key in settings:
		var value: Variant = settings[key]
		if not can_author_value(value):
			continue
		var label := Label.new()
		label.text = String(key).capitalize()
		label.custom_minimum_size.x = LABEL_WIDTH
		label.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
		var control := make_value_control(value, key)
		control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var row := HBoxContainer.new()
		row.add_child(label)
		row.add_child(control)
		container.add_child(row)
		keys.append(StringName(key))
		controls.append(control)
		types.append(typeof(value))


func render_schema(container: Container, schema: Array) -> void:
	var settings := { }
	for entry: Dictionary in schema:
		settings[entry.name] = type_convert("", entry.get("type", TYPE_STRING))
	render_settings(container, settings)


func as_dictionary() -> Dictionary:
	var values := { }
	for at in controls.size():
		values[keys[at]] = value_from_control(controls[at], types[at])
	return values


func as_array() -> Array:
	var values := []
	for at in controls.size():
		values.append(value_from_control(controls[at], types[at]))
	return values


static func make_value_control(
		value: Variant,
		field_name: StringName = &"",
) -> Control:
	match typeof(value):
		TYPE_BOOL:
			var box := CheckBox.new()
			box.button_pressed = value
			return box
		TYPE_INT, TYPE_FLOAT:
			var spin := SpinBox.new()
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.step = 1.0 if value is int else 0.01
			spin.value = value
			return spin
		TYPE_PACKED_STRING_ARRAY:
			var entries := STRING_LIST_EDIT.instantiate()
			entries.set_value(value)
			return entries
		TYPE_ARRAY when field_name == &"ice_servers":
			var servers := ICE_SERVERS_EDIT.instantiate()
			servers.set_value(value)
			return servers
		TYPE_ARRAY:
			var structured := LineEdit.new()
			structured.text = JSON.stringify(value)
			return structured
		_:
			var edit := LineEdit.new()
			edit.text = str(value)
			return edit


static func value_from_control(control: Control, value_type: int) -> Variant:
	match value_type:
		TYPE_BOOL:
			return (control as CheckBox).button_pressed
		TYPE_INT, TYPE_FLOAT:
			return type_convert((control as SpinBox).value, value_type)
		TYPE_PACKED_STRING_ARRAY:
			return control.call(&"get_value")
		TYPE_ARRAY when control.has_method(&"get_value"):
			return control.call(&"get_value")
		TYPE_ARRAY:
			var text := (control as LineEdit).text
			var reader := JSON.new()
			if reader.parse(text) == OK and reader.data is Array:
				return reader.data
			push_warning(
				(
						"The input holding '%s' is not a JSON array, so it was "
						+ "ignored. Write it like [1, 2]."
				) % text,
			)
			return null
		_:
			return type_convert((control as LineEdit).text, value_type)


static func can_author_value(value: Variant) -> bool:
	return typeof(value) not in [
		TYPE_NIL,
		TYPE_OBJECT,
		TYPE_CALLABLE,
		TYPE_SIGNAL,
		TYPE_RID,
	]
