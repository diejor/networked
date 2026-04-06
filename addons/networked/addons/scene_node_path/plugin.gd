@tool
extends EditorPlugin

const InspectorPlugin = preload("uid://di5tdm30ixtee")

const ContextMenuPlugin = preload("uid://bss1lwoehwwcm")

var inspector_plugin: EditorInspectorPlugin
var context_menu_plugin: EditorContextMenuPlugin

func _enter_tree() -> void:
	inspector_plugin = InspectorPlugin.new()
	add_inspector_plugin(inspector_plugin)
	
	context_menu_plugin = ContextMenuPlugin.new()
	add_context_menu_plugin(EditorContextMenuPlugin.CONTEXT_SLOT_SCENE_TREE, context_menu_plugin)

func _exit_tree() -> void:
	if inspector_plugin:
		remove_inspector_plugin(inspector_plugin)
		
	if context_menu_plugin:
		remove_context_menu_plugin(context_menu_plugin)
