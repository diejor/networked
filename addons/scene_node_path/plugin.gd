@tool
extends EditorPlugin

const InspectorPlugin = preload("uid://di5tdm30ixtee")

var inspector_plugin: EditorInspectorPlugin
var context_menu_plugin: SceneNodeContextMenu

func _enter_tree() -> void:
	add_inspector_plugin(InspectorPlugin.new())
	
	context_menu_plugin = SceneNodeContextMenu.new()
	add_context_menu_plugin(EditorContextMenuPlugin.CONTEXT_SLOT_SCENE_TREE, context_menu_plugin)

func _exit_tree() -> void:
	if inspector_plugin:
		remove_inspector_plugin(inspector_plugin)
		
	if context_menu_plugin:
		remove_context_menu_plugin(context_menu_plugin)


# ==========================================
# The Native Context Menu Integration
# ==========================================

class SceneNodeContextMenu extends EditorContextMenuPlugin:
	
	var uid_icon := EditorInterface.get_editor_theme().get_icon("UID", "EditorIcons")
	var npath_icon := EditorInterface.get_editor_theme().get_icon("NodePath", "EditorIcons")
	
	func _popup_menu(paths: PackedStringArray) -> void:
		var selected_nodes: Array[Node] = EditorInterface.get_selection().get_selected_nodes()
		
		if selected_nodes.size() == 1:
			add_context_menu_item("Copy Scene Node Path", _copy_uid, uid_icon)
			add_context_menu_item("Copy Scene Node Path ", _copy_abs, npath_icon)
			
	func _copy_uid(paths: PackedStringArray) -> void:
		_process_copy(true)
		
	func _copy_abs(paths: PackedStringArray) -> void:
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
