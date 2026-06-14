## Main editor plugin for the Networked addon.
##
## Initializes the logging system, registers singletons, and adds the
## NetwLog dock and debugger plugin to the Godot editor.
@tool
extends EditorPlugin

const DEBUG_REPORTER_PATH = "res://addons/networked/debug/core/bootstrap.gd"
const SCENE_NODE_PATH_PLUGIN_PATH = "res://addons/networked/addons/scene_node_path/plugin.gd"
const NETW_LOG_EDITOR_PATH = "res://addons/networked/debug/editor/log_panel/log_editor.gd"
const DEBUGGER_PLUGIN_PATH = "res://addons/networked/debug/editor/plugin.gd"

## Reference to the SceneNodePath editor plugin instance.
var scene_node_path_plugin: EditorPlugin

## Reference to the NetwLog editor dock control.
var log_editor: Control

## Reference to the networked debugger plugin instance.
var _debugger_plugin: EditorDebuggerPlugin

var _autoload_registered := false


func _enter_tree() -> void:
	NetwLog.initialize(get_script().get_path().get_base_dir())
	if DisplayServer.get_name() == "headless":
		return

	_register_settings()
	add_autoload_singleton("NetworkedDebugger", DEBUG_REPORTER_PATH)
	_autoload_registered = true

	var scene_node_path_script: GDScript = load(SCENE_NODE_PATH_PLUGIN_PATH)
	scene_node_path_plugin = scene_node_path_script.new()
	add_child(scene_node_path_plugin)

	var log_editor_script: GDScript = load(NETW_LOG_EDITOR_PATH)
	log_editor = log_editor_script.new()
	log_editor.name = "NetwLog"
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, log_editor)

	var debugger_script: GDScript = load(DEBUGGER_PLUGIN_PATH)
	_debugger_plugin = debugger_script.new()
	add_debugger_plugin(_debugger_plugin)


func _exit_tree() -> void:
	if _autoload_registered:
		remove_autoload_singleton("NetworkedDebugger")
		_autoload_registered = false

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
		"Log",
		"EditorIcons",
	)


func _register_settings() -> void:
	var setting_name := "networked/debug/auto_tile_instances"
	if not ProjectSettings.has_setting(setting_name):
		ProjectSettings.set_setting(setting_name, true)

	ProjectSettings.set_initial_value(setting_name, true)
	ProjectSettings.add_property_info(
		{
			"name": setting_name,
			"type": TYPE_BOOL,
			"hint": PROPERTY_HINT_NONE,
		},
	)

	var turn_credentials_setting := "networked/webrtc/turn_credentials_url"
	if not ProjectSettings.has_setting(turn_credentials_setting):
		ProjectSettings.set_setting(turn_credentials_setting, "")

	ProjectSettings.set_initial_value(turn_credentials_setting, "")
	ProjectSettings.add_property_info(
		{
			"name": turn_credentials_setting,
			"type": TYPE_STRING,
			"hint": PROPERTY_HINT_NONE,
		},
	)

	var turn_headers_setting := "networked/webrtc/turn_credentials_headers"
	if not ProjectSettings.has_setting(turn_headers_setting):
		ProjectSettings.set_setting(
			turn_headers_setting,
			PackedStringArray(),
		)

	ProjectSettings.set_initial_value(
		turn_headers_setting,
		PackedStringArray(),
	)
	ProjectSettings.add_property_info(
		{
			"name": turn_headers_setting,
			"type": TYPE_PACKED_STRING_ARRAY,
			"hint": PROPERTY_HINT_NONE,
		},
	)
