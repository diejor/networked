@tool
extends ConfirmationDialog

signal path_selected(scene_path: String, node_path: String)

var forced_allowed_class: String = "Node"
var is_builtin_class: bool = true
var custom_allowed_script: Script = null
var custom_icon: Texture2D = null
var current_scene_path: String = ""

var file_dialog: EditorFileDialog
var node_tree: Tree
var current_scene_lbl: LineEdit
var search_box: LineEdit
var show_all_btn: CheckButton
var allowed_icon: TextureRect
var allowed_class_lbl: Label
var temp_scene_instance: Node

func _init() -> void:
	title = "Select Scene Node Path"
	size = Vector2(450, 600)
	confirmed.connect(_on_confirmed)
	canceled.connect(_clean_up_scene)
	about_to_popup.connect(func(): search_box.call_deferred("grab_focus"))
	
	_build_ui()

func _build_ui() -> void:
	file_dialog = EditorFileDialog.new()
	file_dialog.access = EditorFileDialog.ACCESS_RESOURCES
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.tscn", "PackedScene")
	file_dialog.file_selected.connect(_on_file_selected)
	EditorInterface.get_base_control().add_child(file_dialog)

	var dialog_vbox = VBoxContainer.new()
	add_child(dialog_vbox)

	var scene_header = HBoxContainer.new()
	var change_scene_btn = Button.new()
	change_scene_btn.text = "Change Scene"
	change_scene_btn.pressed.connect(func(): hide(); file_dialog.popup_file_dialog())
	scene_header.add_child(change_scene_btn)
	
	current_scene_lbl = LineEdit.new()
	current_scene_lbl.editable = false
	current_scene_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scene_header.add_child(current_scene_lbl)
	dialog_vbox.add_child(scene_header)

	var allowed_hbox = HBoxContainer.new()
	var allowed_lbl = Label.new()
	allowed_lbl.text = "Allowed:"
	
	allowed_icon = TextureRect.new()
	allowed_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	
	allowed_class_lbl = Label.new()
	allowed_class_lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	
	allowed_hbox.add_child(allowed_lbl)
	allowed_hbox.add_child(allowed_icon)
	allowed_hbox.add_child(allowed_class_lbl)
	dialog_vbox.add_child(allowed_hbox)

	var search_hbox = HBoxContainer.new()
	search_box = LineEdit.new()
	search_box.placeholder_text = "Filter Nodes..."
	search_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	search_box.clear_button_enabled = true
	search_box.text_changed.connect(_rebuild_tree)
	search_hbox.add_child(search_box)
	
	show_all_btn = CheckButton.new()
	show_all_btn.text = "Show All"
	show_all_btn.button_pressed = true
	show_all_btn.toggled.connect(func(_toggled): _rebuild_tree(search_box.text))
	search_hbox.add_child(show_all_btn)
	dialog_vbox.add_child(search_hbox)

	node_tree = Tree.new()
	node_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	node_tree.item_activated.connect(_on_confirmed)
	dialog_vbox.add_child(node_tree)

func _ready() -> void:
	search_box.right_icon = get_theme_icon("Search", "EditorIcons")

func setup_and_open(scene_path: String, target_class: String, is_builtin: bool, custom_script: Script, icon: Texture2D) -> void:
	forced_allowed_class = target_class
	is_builtin_class = is_builtin
	custom_allowed_script = custom_script
	custom_icon = icon
	
	allowed_class_lbl.text = forced_allowed_class
	allowed_icon.texture = _get_class_icon(forced_allowed_class)
	
	if scene_path.is_empty() or not ResourceLoader.exists(scene_path):
		file_dialog.popup_file_dialog()
	else:
		_load_scene(scene_path)
		popup_centered()

func _load_scene(path: String) -> void:
	current_scene_path = path
	current_scene_lbl.text = path.get_file()
	
	var packed_scene: PackedScene = load(path)
	if packed_scene:
		_clean_up_scene()
		temp_scene_instance = packed_scene.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
		_rebuild_tree("")

func _on_file_selected(path: String) -> void:
	var uid: int = ResourceLoader.get_resource_uid(path)
	current_scene_path = ResourceUID.id_to_text(uid) if uid != ResourceUID.INVALID_ID else path
	
	_load_scene(path)
	call_deferred("popup_centered")

func _on_confirmed() -> void:
	var selected: TreeItem = node_tree.get_selected()
	if not selected:
		return
		
	var meta = selected.get_metadata(0)
	if typeof(meta) == TYPE_DICTIONARY:
		path_selected.emit(current_scene_path, meta.get("path", ""))
		
	hide()
	_clean_up_scene()

func _clean_up_scene() -> void:
	if temp_scene_instance:
		temp_scene_instance.queue_free()
		temp_scene_instance = null
	search_box.text = ""

func _rebuild_tree(filter: String = "") -> void:
	node_tree.clear()
	if not temp_scene_instance:
		return
		
	var root: TreeItem = node_tree.create_item()
	node_tree.hide_root = true
	_add_node_to_tree(temp_scene_instance, root, filter, show_all_btn.button_pressed)

func _is_node_allowed(node: Node) -> bool:
	if forced_allowed_class.is_empty() or forced_allowed_class == "Node":
		return true
	if is_builtin_class:
		return node.is_class(forced_allowed_class)
	if custom_allowed_script and node.get_script():
		var current_script: Script = node.get_script()
		while current_script:
			if current_script == custom_allowed_script:
				return true
			current_script = current_script.get_base_script()
	return false

func _add_node_to_tree(node: Node, parent_item: TreeItem, filter: String, show_all: bool) -> TreeItem:
	var item: TreeItem = node_tree.create_item(parent_item)
	item.set_text(0, node.name)
	
	var final_path: String = _get_robust_path_string(temp_scene_instance, node)
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
	if has_theme_icon(class_name_str, "EditorIcons"):
		return get_theme_icon(class_name_str, "EditorIcons")
	if custom_icon:
		return custom_icon
	return get_theme_icon("Node", "EditorIcons")

func _get_robust_path_string(root: Node, target: Node) -> String:
	if root == target:
		return "."

	var path := ""
	var current: Node = target

	while current != root and current != null:
		if current.unique_name_in_owner:
			if current.owner == root:
				path = "%" + current.name + (("/" + path) if not path.is_empty() else "")
				break
			elif current.owner != null:
				path = "%" + current.name + (("/" + path) if not path.is_empty() else "")
				current = current.owner
				continue

		path = current.name + (("/" + path) if not path.is_empty() else "")
		current = current.get_parent()

	return path
