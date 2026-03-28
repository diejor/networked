@tool
extends EditorContextMenuPlugin

func _popup_menu(_paths: PackedStringArray) -> void:
	var selected_nodes: Array[Node] = EditorInterface.get_selection().get_selected_nodes()
	
	if selected_nodes.size() == 1:
		var theme := EditorInterface.get_editor_theme()
		var uid_icon := theme.get_icon("UID", "EditorIcons")
		var npath_icon := theme.get_icon("NodePath", "EditorIcons")
		
		add_context_menu_item("Copy Path (UID)", _copy_uid, uid_icon)
		add_context_menu_item("Copy Path (Absolute)", _copy_abs, npath_icon)
		
func _copy_uid(_paths: PackedStringArray) -> void:
	_process_copy(true)
	
func _copy_abs(_paths: PackedStringArray) -> void:
	_process_copy(false)
	
func _process_copy(use_uid: bool) -> void:
	var selected_nodes: Array[Node] = EditorInterface.get_selection().get_selected_nodes()
	if selected_nodes.is_empty():
		return
		
	var target_node: Node = selected_nodes[0]
	var edited_scene_root: Node = EditorInterface.get_edited_scene_root()
	
	if not edited_scene_root:
		push_warning("SceneNodePath: Cannot copy path. No scene is currently open.")
		return
		
	var scene_file: String = edited_scene_root.scene_file_path
	if scene_file.is_empty():
		push_warning("SceneNodePath: Cannot copy path. Please save the scene first.")
		return
		
	var node_path_str: String
	if target_node.unique_name_in_owner:
		node_path_str = "%" + target_node.name
	else:
		node_path_str = str(edited_scene_root.get_path_to(target_node))
		
	if use_uid:
		var uid: int = ResourceLoader.get_resource_uid(scene_file)
		if uid != ResourceUID.INVALID_ID:
			scene_file = ResourceUID.id_to_text(uid)
			
	var final_string: String = "%s::%s" % [scene_file, node_path_str]
	DisplayServer.clipboard_set(final_string)
	
	print_rich("[color=green]Copied to clipboard:[/color] ", final_string)
