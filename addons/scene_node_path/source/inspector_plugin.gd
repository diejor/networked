@tool
extends EditorInspectorPlugin

const SceneNodePathEditorProperty = preload("uid://bbb777txx33hk")

func _can_handle(_object: Object) -> bool:
	return true

func _parse_property(object: Object, type: Variant.Type, name: String, _hint_type: PropertyHint, hint_string: String, _usage_flags: int, _wide: bool) -> bool:
	if type != TYPE_OBJECT or not "SceneNodePath" in hint_string:
		return false

	var editor := SceneNodePathEditorProperty.new()
	
	editor.config.target_class = _extract_allowed_class(hint_string)
	editor.is_csharp_mode = _is_csharp_object(object)
	
	add_property_editor(name, editor)
	return true

func _extract_allowed_class(hint_string: String) -> String:
	if ":" not in hint_string:
		return "Node"
		
	var parts := hint_string.split(":")
	var potential_type := parts[-1].strip_edges()
	
	if potential_type in ["SceneNodePath", "SceneNodePathCS"]:
		return "Node"
		
	return potential_type

func _is_csharp_object(object: Object) -> bool:
	var script: Script = object.get_script()
	return script != null and script.resource_path.ends_with(".cs")
