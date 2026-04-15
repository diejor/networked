@tool
extends EditorPlugin

const SceneNodePathPlugin = preload("uid://dtj5ucl1iy3ug")
var scene_node_path_plugin: EditorPlugin

var log_editor: Control
var _debugger_plugin: EditorDebuggerPlugin

func _enter_tree() -> void:
	# Pass the addon root so NetLog can produce addon-relative module paths.
	# This makes saved overrides stable even if the addon directory is moved.
	NetLog.initialize(get_script().get_path().get_base_dir())

	add_autoload_singleton("NetworkedDebugger", "res://addons/networked/debug/networked_debug_reporter.gd")

	scene_node_path_plugin = SceneNodePathPlugin.new()
	add_child(scene_node_path_plugin)

	log_editor = preload("uid://b2dp22x17yufo").new()
	log_editor.custom_minimum_size.y = 200
	add_control_to_bottom_panel(log_editor, "NetLog")

	_debugger_plugin = preload("uid://b2lc6aalf32kx").new()
	add_debugger_plugin(_debugger_plugin)

func _exit_tree() -> void:
	remove_autoload_singleton("NetworkedDebugger")

	if is_instance_valid(scene_node_path_plugin):
		scene_node_path_plugin.queue_free()

	if log_editor:
		remove_control_from_bottom_panel(log_editor)
		log_editor.queue_free()

	if _debugger_plugin:
		remove_debugger_plugin(_debugger_plugin)
		_debugger_plugin = null

func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon("Log", "EditorIcons")
