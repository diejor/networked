@tool
class_name EditorIconUtils
extends RefCounted

## Resolves the best Godot Editor icon based on a custom texture, script, and class name.
static func resolve_icon(preferred_icon: Texture2D, script: Script, class_name_str: String) -> Texture2D:
	if not Engine.is_editor_hint():
		return null
	
	var theme: Theme = EditorInterface.get_editor_theme()
	
	if preferred_icon != null and preferred_icon.get_width() > 0:
		return preferred_icon
	
	if script:
		var global_name: String = script.get_global_name()
		if not global_name.is_empty() and theme.has_icon(global_name, "EditorIcons"):
			var s_icon: Texture2D = theme.get_icon(global_name, "EditorIcons")
			if s_icon and s_icon.get_width() > 0:
				return s_icon
	
	var current_class: String = class_name_str
	if script and not ClassDB.class_exists(current_class):
		current_class = script.get_instance_base_type()
	
	while not current_class.is_empty():
		if theme.has_icon(current_class, "EditorIcons"):
			var n_icon: Texture2D = theme.get_icon(current_class, "EditorIcons")
			if n_icon and n_icon.get_width() > 0:
				return n_icon
		current_class = ClassDB.get_parent_class(current_class)
	
	return theme.get_icon("Node", "EditorIcons")
