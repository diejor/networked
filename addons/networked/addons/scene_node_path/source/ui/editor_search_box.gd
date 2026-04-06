@tool
extends Tree

var target_class: String = "Node"
var is_builtin: bool = true
var custom_script: Script = null
var custom_icon: Texture2D = null
var root_scene_node: Node = null

func configure(p_target_class: String, p_is_builtin: bool, p_custom_script: Script, p_custom_icon: Texture2D) -> void:
	target_class = p_target_class
	is_builtin = p_is_builtin
	custom_script = p_custom_script
	custom_icon = p_custom_icon

func rebuild(scene_instance: Node, filter: String, show_all: bool) -> void:
	clear()
	root_scene_node = scene_instance
	if not root_scene_node:
		return
		
	var root_item: TreeItem = create_item()
	hide_root = true
	_add_node_to_tree(root_scene_node, root_item, filter, show_all)

func _is_node_allowed(node: Node) -> bool:
	if target_class.is_empty() or target_class == "Node":
		return true
	if is_builtin:
		return node.is_class(target_class)
	if custom_script and node.get_script():
		var current_script: Script = node.get_script()
		while current_script:
			if current_script == custom_script:
				return true
			current_script = current_script.get_base_script()
	return false

func _add_node_to_tree(node: Node, parent_item: TreeItem, filter: String, show_all: bool) -> TreeItem:
	var item: TreeItem = create_item(parent_item)
	item.set_text(0, node.name)
	
	var final_path: String = _get_robust_path_string(root_scene_node, node)
	item.set_metadata(0, {"path": final_path})
	item.set_icon(0, _get_class_icon(node.get_class()))
		
	var is_allowed: bool = _is_node_allowed(node)
	if not is_allowed:
		item.set_selectable(0, false)
		item.set_custom_color(0, Color(0.5, 0.5, 0.5, 0.5))
	
	var matches_filter: bool = filter.is_empty() or node.name.to_lower().contains(filter.to_lower())
	var is_valid_match: bool = matches_filter and (is_allowed or show_all)
	var has_valid_child: bool = false
	
	for child in node.get_children():
		var child_item: TreeItem = _add_node_to_tree(child, item, filter, show_all)
		if child_item != null:
			has_valid_child = true
			
	if not is_valid_match and not has_valid_child:
		item.free()
		return null
		
	if (not filter.is_empty() or not show_all) and has_valid_child:
		item.collapsed = false
		
	return item

func _get_class_icon(class_name_str: String) -> Texture2D:
	if Engine.is_editor_hint() and EditorInterface.get_editor_theme().has_icon(class_name_str, "EditorIcons"):
		return EditorInterface.get_editor_theme().get_icon(class_name_str, "EditorIcons")
	if custom_icon:
		return custom_icon
	
	if Engine.is_editor_hint():
		return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")
	return null

func _get_robust_path_string(root: Node, target: Node) -> String:
	if root == target: return "."
	var path := ""
	var current: Node = target

	while current != root and current != null:
		if current.unique_name_in_owner:
			if current.owner == root or current.owner != null:
				path = "%" + current.name + (("/" + path) if not path.is_empty() else "")
				if current.owner == root: break
				current = current.owner
				continue
		path = current.name + (("/" + path) if not path.is_empty() else "")
		current = current.get_parent()

	return path
