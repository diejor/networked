extends EditorInspectorPlugin

## Parses the editor inspector to inject the custom Scene Node Path editor.

const SceneNodePathEditorProperty = preload("uid://bbb777txx33hk")

func _can_handle(object: Object) -> bool:
	return true

func _parse_property(object: Object, type: Variant.Type, name: String, hint_type: PropertyHint, hint_string: String, usage_flags: int, wide: bool) -> bool:
	if type != TYPE_OBJECT:
		return false
	
	if not "SceneNodePath" in hint_string:
		return false
	
	var allowed_class: String = "Node"
	
	if ":" in hint_string:
		var parts = hint_string.split(":")
		var potential_type = parts[-1].strip_edges()
		if potential_type != "SceneNodePath":
			allowed_class = potential_type
	
	var editor = SceneNodePathEditorProperty.new()
	editor.forced_allowed_class = allowed_class
	add_property_editor(name, editor)
	return true
