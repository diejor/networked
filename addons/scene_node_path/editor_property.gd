@tool
extends EditorProperty

const PickerScript = preload("uid://bi68kvs2if4hr")

static var global_class_cache: Dictionary = {}
static var global_icon_cache: Dictionary = {}

var forced_allowed_class: String = "Node"
var is_builtin_class: bool = true
var custom_allowed_script: Script = null
var custom_icon: Texture2D = null

var _cached_scene_path: String = ""
var _cached_node_path: String = ""
var _cached_mod_time: int = 0
var _cached_is_broken: bool = true
var _cached_dynamic_class: String = "Node"

var main_btn: Button
var menu_btn: MenuButton
var picker_dialog: ConfirmationDialog

func _init() -> void:
	var container = HBoxContainer.new()
	add_child(container)

	main_btn = Button.new()
	main_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main_btn.clip_text = true
	main_btn.alignment = HORIZONTAL_ALIGNMENT_CENTER
	main_btn.pressed.connect(_on_main_button_pressed)
	container.add_child(main_btn)

	menu_btn = MenuButton.new()
	menu_btn.flat = true
	var popup: PopupMenu = menu_btn.get_popup()
	popup.add_item("Change Scene", 0)
	popup.add_item("Copy as UID", 1)
	popup.add_item("Copy as Path", 2)
	popup.add_separator()
	popup.add_item("Clear", 4)
	popup.id_pressed.connect(_on_menu_id_pressed)
	container.add_child(menu_btn)
	
	picker_dialog = PickerScript.new()
	picker_dialog.path_selected.connect(_on_path_selected)
	add_child(picker_dialog)

func _enter_tree() -> void:
	_resolve_class_type()
	
	menu_btn.icon = get_theme_icon("GuiTabMenuHl", "EditorIcons")
	var popup: PopupMenu = menu_btn.get_popup()
	popup.set_item_icon(0, get_theme_icon("Load", "EditorIcons"))
	popup.set_item_icon(1, get_theme_icon("UID", "EditorIcons"))
	popup.set_item_icon(2, get_theme_icon("NodePath", "EditorIcons"))
	popup.set_item_icon(4, get_theme_icon("Clear", "EditorIcons"))
	
	var dark_stylebox: StyleBox = get_theme_stylebox("normal", "LineEdit")
	main_btn.add_theme_stylebox_override("normal", dark_stylebox)
	main_btn.add_theme_stylebox_override("hover", dark_stylebox)
	main_btn.add_theme_stylebox_override("focus", dark_stylebox)
	main_btn.add_theme_stylebox_override("pressed", dark_stylebox)

func _resolve_class_type() -> void:
	if forced_allowed_class == "Node" or ClassDB.class_exists(forced_allowed_class):
		is_builtin_class = true
		return

	is_builtin_class = false
	if global_class_cache.has(forced_allowed_class):
		custom_allowed_script = global_class_cache[forced_allowed_class]
		custom_icon = global_icon_cache.get(forced_allowed_class)
		return

	for class_data in ProjectSettings.get_global_class_list():
		if class_data["class"] == forced_allowed_class:
			custom_allowed_script = load(class_data["path"])
			global_class_cache[forced_allowed_class] = custom_allowed_script
			var icon_path: String = class_data.get("icon", "")
			if not icon_path.is_empty():
				custom_icon = load(icon_path)
				global_icon_cache[forced_allowed_class] = custom_icon
			break

func _update_property() -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	main_btn.remove_theme_color_override("font_color")
	
	if not res or res.scene_path.is_empty():
		if res and "_editor_property_warnings" in res:
			res._editor_property_warnings = "Scene path is not assigned."
		_apply_ui_state(false, false, "Assign Scene...", "Node")
		return

	var real_path: String = _get_actual_scene_path(res.scene_path)
	var scene_name: String = real_path.get_file().get_basename()
	
	if res.node_path.is_empty():
		if "_editor_property_warnings" in res:
			res._editor_property_warnings = "Node path is not assigned."
		_apply_ui_state(true, false, "%s::(Missing Node!)" % scene_name, "Node")
		return

	var current_mod_time: int = FileAccess.get_modified_time(real_path)
	if _is_cache_valid(res, current_mod_time):
		var display_text: String = "%s::%s" % [scene_name, res.node_path]
		if _cached_is_broken:
			display_text += " (Broken!)"
		
		_apply_ui_state(_cached_is_broken, true, display_text, _cached_dynamic_class)
		return

	_update_cache_and_validate(real_path, res, current_mod_time)

func _is_cache_valid(res: Variant, current_mod_time: int) -> bool:
	return res.scene_path == _cached_scene_path and res.node_path == _cached_node_path and current_mod_time == _cached_mod_time

func _update_cache_and_validate(real_path: String, res: Variant, current_mod_time: int) -> void:
	var is_broken: bool = true
	var dynamic_class: String = forced_allowed_class
	var warning_msg: String = ""

	if not ResourceLoader.exists(real_path):
		warning_msg = "Scene file does not exist at path: %s" % real_path
	else:
		var packed: PackedScene = load(real_path)
		if packed:
			var temp_instance: Node = packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
			if temp_instance:
				var target_node: Node = temp_instance.get_node_or_null(res.node_path)
				if target_node:
					is_broken = false
					dynamic_class = target_node.get_class()
					warning_msg = ""
				else:
					warning_msg = "Node '%s' does not exist in the referenced scene." % res.node_path
				temp_instance.free()
		else:
			warning_msg = "Failed to load PackedScene."

	if "_editor_property_warnings" in res:
		res._editor_property_warnings = warning_msg

	_cached_scene_path = res.scene_path
	_cached_node_path = res.node_path
	_cached_mod_time = current_mod_time
	_cached_is_broken = is_broken
	_cached_dynamic_class = dynamic_class

	var scene_name: String = real_path.get_file().get_basename()
	var display_text: String = "%s::%s" % [scene_name, res.node_path]
	if is_broken:
		display_text += " (Broken!)"

	_apply_ui_state(is_broken, true, display_text, dynamic_class)

func _apply_ui_state(is_broken: bool, is_assigned: bool, display_text: String, dynamic_class: String) -> void:
	main_btn.text = display_text
	
	if not is_assigned:
		main_btn.icon = get_theme_icon("NodeWarning", "EditorIcons") if is_broken else null
		if is_broken:
			main_btn.add_theme_color_override("font_color", get_theme_color("warning_color", "Editor"))
		return

	if is_broken:
		main_btn.icon = get_theme_icon("NodeWarning", "EditorIcons")
		main_btn.add_theme_color_override("font_color", get_theme_color("warning_color", "Editor"))
	else:
		main_btn.icon = picker_dialog._get_class_icon(dynamic_class)

func _get_actual_scene_path(path: String) -> String:
	if path.begins_with("uid://"):
		var id: int = ResourceUID.text_to_id(path)
		if ResourceUID.has_id(id):
			return ResourceUID.get_id_path(id)
	return path

func _get_or_create_resource() -> Variant:
	var current_res = get_edited_object()[get_edited_property()]
	if current_res:
		return current_res
	return SceneNodePath.new()

func _on_main_button_pressed() -> void:
	var res: Variant = _get_or_create_resource()
	var real_path: String = _get_actual_scene_path(res.scene_path)
	picker_dialog.setup_and_open(real_path, forced_allowed_class, is_builtin_class, custom_allowed_script, custom_icon)

func _on_path_selected(scene_path: String, node_path: String) -> void:
	var res: Variant = _get_or_create_resource()
	
	var final_scene_path: String = scene_path
	if final_scene_path.begins_with("res://") and ResourceLoader.exists(final_scene_path):
		var uid: int = ResourceLoader.get_resource_uid(final_scene_path)
		if uid != ResourceUID.INVALID_ID:
			final_scene_path = ResourceUID.id_to_text(uid)
			
	res.scene_path = final_scene_path
	res.node_path = node_path
	emit_changed(get_edited_property(), res)

func _on_menu_id_pressed(id: int) -> void:
	match id:
		0: picker_dialog.file_dialog.popup_file_dialog()
		1: _copy_to_clipboard(true)
		2: _copy_to_clipboard(false)
		4: emit_changed(get_edited_property(), null)

func _copy_to_clipboard(use_uid: bool) -> void:
	var res: Variant = get_edited_object()[get_edited_property()]
	if not res: return
	var path_to_copy: String = res.scene_path
	if not use_uid:
		path_to_copy = _get_actual_scene_path(res.scene_path)
	elif not path_to_copy.begins_with("uid://"):
		var uid: int = ResourceLoader.get_resource_uid(path_to_copy)
		if uid != ResourceUID.INVALID_ID:
			path_to_copy = ResourceUID.id_to_text(uid)
	DisplayServer.clipboard_set("%s::%s" % [path_to_copy, res.node_path])
