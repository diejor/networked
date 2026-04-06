@tool
extends EditorProperty

const NODE_PICKER_DIALOG := preload("uid://b5gj8b5qtiard")
const PROPERTY_PICKER_UI := preload("uid://dey3omtjsnn3s")

static var global_class_cache: Dictionary = {}
static var global_icon_cache: Dictionary = {}


var config := FilterConfig.new()
var cache := CacheState.new()
var is_csharp_mode: bool = false

var ui_view: PropertyPickerUI
var picker_dialog: ConfirmationDialog

func _init() -> void:
	ui_view = PROPERTY_PICKER_UI.instantiate()
	add_child(ui_view)

	ui_view.open_picker_requested.connect(_on_open_picker)
	ui_view.copy_uid_requested.connect(func(): _copy_to_clipboard(true))
	ui_view.copy_path_requested.connect(func(): _copy_to_clipboard(false))
	ui_view.clear_requested.connect(func(): emit_changed(get_edited_property(), null))
	
	picker_dialog = NODE_PICKER_DIALOG.instantiate()
	picker_dialog.path_selected.connect(_on_path_selected)
	add_child(picker_dialog)

func _enter_tree() -> void:
	_resolve_class_configuration()

func _resolve_class_configuration() -> void:
	if config.target_class == "Node" or ClassDB.class_exists(config.target_class):
		config.is_builtin = true
		return

	config.is_builtin = false
	if global_class_cache.has(config.target_class):
		config.custom_script = global_class_cache[config.target_class]
		config.icon = global_icon_cache.get(config.target_class)
		return

	for class_data in ProjectSettings.get_global_class_list():
		if class_data["class"] == config.target_class:
			config.custom_script = load(class_data["path"])
			global_class_cache[config.target_class] = config.custom_script
			var icon_path: String = class_data.get("icon", "")
			if not icon_path.is_empty():
				config.icon = load(icon_path)
				global_icon_cache[config.target_class] = config.icon
			break

func _update_property() -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	var current_scene_path: String = DuckTypeHelper.get_duck(res, "scene_path", "")
	var current_node_path: String = DuckTypeHelper.get_duck(res, "node_path", "")
	
	if not res or current_scene_path.is_empty():
		DuckTypeHelper.set_duck(res, "_editor_property_warnings", "Scene path is not assigned.")
		ui_view.set_empty_state()
		return
	
	var real_path: String = _get_actual_scene_path(current_scene_path)
	var scene_name: String = real_path.get_file().get_basename()
	
	if current_node_path.is_empty():
		DuckTypeHelper.set_duck(res, "_editor_property_warnings", "Node path is not assigned.")
		ui_view.set_broken_state("%s::(Missing Node!)" % scene_name, get_theme_color("warning_color", "Editor"), get_theme_icon("NodeWarning", "EditorIcons"))
		return
	
	var current_mod_time: int = FileAccess.get_modified_time(real_path)
	
	if not cache.is_valid(current_scene_path, current_node_path, current_mod_time):
		cache = ScenePathValidator.validate(real_path, current_node_path, current_mod_time)
		DuckTypeHelper.set_duck(res, "_editor_property_warnings", cache.warning_msg)
	
	var display_text := "%s::%s" % [scene_name, current_node_path]
	if cache.is_broken:
		display_text += " (Broken!)"
		ui_view.set_broken_state(display_text, get_theme_color("warning_color", "Editor"), get_theme_icon("NodeWarning", "EditorIcons"))
	else:
		ui_view.set_valid_state(display_text, _get_class_icon(cache.dynamic_class))

func _on_path_selected(scene_path: String, node_path: String) -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	if not res: res = DuckTypeHelper.create_resource(is_csharp_mode)
	
	var final_scene_path: String = scene_path
	if final_scene_path.begins_with("res://") and ResourceLoader.exists(final_scene_path):
		var uid: int = ResourceLoader.get_resource_uid(final_scene_path)
		if uid != ResourceUID.INVALID_ID:
			final_scene_path = ResourceUID.id_to_text(uid)
			
	DuckTypeHelper.set_duck(res, "scene_path", final_scene_path)
	DuckTypeHelper.set_duck(res, "node_path", node_path)
	emit_changed(get_edited_property(), res)

func _on_open_picker() -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	var current_scene_path: String = DuckTypeHelper.get_duck(res, "scene_path", "")
	var real_path: String = _get_actual_scene_path(current_scene_path)
	
	picker_dialog.setup_and_open(real_path, config)

func _copy_to_clipboard(use_uid: bool) -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	if not res: return
	
	var current_scene_path: String = DuckTypeHelper.get_duck(res, "scene_path", "")
	var current_node_path: String = DuckTypeHelper.get_duck(res, "node_path", "")
	var path_to_copy: String = current_scene_path
	
	if not use_uid:
		path_to_copy = _get_actual_scene_path(current_scene_path)
	elif not path_to_copy.begins_with("uid://"):
		var uid: int = ResourceLoader.get_resource_uid(path_to_copy)
		if uid != ResourceUID.INVALID_ID:
			path_to_copy = ResourceUID.id_to_text(uid)
			
	DisplayServer.clipboard_set("%s::%s" % [path_to_copy, current_node_path])

func _get_actual_scene_path(path: String) -> String:
	if path.begins_with("uid://"):
		var id: int = ResourceUID.text_to_id(path)
		if ResourceUID.has_id(id): return ResourceUID.get_id_path(id)
	return path

func _get_class_icon(class_name_str: String) -> Texture2D:
	if has_theme_icon(class_name_str, "EditorIcons"): return get_theme_icon(class_name_str, "EditorIcons")
	if config.icon: return config.icon
	return get_theme_icon("Node", "EditorIcons")
