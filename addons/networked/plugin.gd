## Main editor plugin for the Networked addon.
##
## Registers the addon's project settings and its Debug menu items.
@tool
extends EditorPlugin

const _DebugMenu := preload("gdscript/editor/debug_menu.gd")

var _debug_menu: _DebugMenu


func _enter_tree() -> void:
	if DisplayServer.get_name() == "headless":
		return

	_register_settings()
	_debug_menu = _DebugMenu.new(self)
	_debug_menu.attach()


func _exit_tree() -> void:
	if _debug_menu != null:
		_debug_menu.detach()
		_debug_menu = null


func _run_scene(
		_scene: String,
		args: PackedStringArray,
) -> PackedStringArray:
	if _debug_menu == null:
		return args
	return _debug_menu.run_args(args)


func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon(
		"Log",
		"EditorIcons",
	)


func _register_settings() -> void:
	var install_setting := "networked/install_as_default"
	if not ProjectSettings.has_setting(install_setting):
		ProjectSettings.set_setting(install_setting, true)

	ProjectSettings.set_initial_value(install_setting, true)
	ProjectSettings.add_property_info(
		{
			"name": install_setting,
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
