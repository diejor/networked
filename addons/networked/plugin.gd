## Main editor plugin for the Networked addon.
##
## Initializes the logging system, registers singletons, and adds the
## NetwLog dock and debugger plugin to the Godot editor.
@tool
extends EditorPlugin

const SceneNodePathPlugin = preload("uid://dtj5ucl1iy3ug")
const NetwLogEditor = preload("uid://uesyjc4dyxqn")
const DebuggerPlugin = preload("uid://b2lc6aalf32kx")
const DEBUG_REPORTER_PATH = "res://addons/networked/debug/core/reporter.gd"

## Reference to the SceneNodePath editor plugin instance.
var scene_node_path_plugin: EditorPlugin

## Reference to the NetwLog editor dock control.
var log_editor: Control

## Reference to the networked debugger plugin instance.
var _debugger_plugin: EditorDebuggerPlugin


func _enter_tree() -> void:
	NetwLog.initialize(get_script().get_path().get_base_dir())

	_register_settings()
	add_autoload_singleton("NetworkedDebugger", DEBUG_REPORTER_PATH)

	scene_node_path_plugin = SceneNodePathPlugin.new()
	add_child(scene_node_path_plugin)

	log_editor = NetwLogEditor.new()
	log_editor.name = "NetwLog"
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, log_editor)

	_debugger_plugin = DebuggerPlugin.new()
	add_debugger_plugin(_debugger_plugin)


func _exit_tree() -> void:
	remove_autoload_singleton("NetworkedDebugger")

	if is_instance_valid(scene_node_path_plugin):
		scene_node_path_plugin.queue_free()

	if log_editor:
		remove_control_from_docks(log_editor)
		log_editor.queue_free()

	if _debugger_plugin:
		remove_debugger_plugin(_debugger_plugin)
		_debugger_plugin = null


func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon(
		"Log", "EditorIcons"
	)


func _register_settings() -> void:
	var setting_name := "networked/debug/auto_tile_instances"
	if not ProjectSettings.has_setting(setting_name):
		ProjectSettings.set_setting(setting_name, true)
	
	ProjectSettings.set_initial_value(setting_name, true)
	ProjectSettings.add_property_info({
		"name": setting_name,
		"type": TYPE_BOOL,
		"hint": PROPERTY_HINT_NONE,
	})
