@tool
extends ConfirmationDialog

signal path_selected(scene_path: String, node_path: String)

var current_scene_path: String = ""
var file_dialog: EditorFileDialog
var temp_scene_instance: Node

@onready var change_scene_btn: Button = %ChangeSceneBtn
@onready var current_scene_lbl: LineEdit = %CurrentSceneLbl
@onready var allowed_icon: TextureRect = %AllowedIcon
@onready var allowed_class_lbl: Label = %AllowedClassLbl
@onready var search_box: LineEdit = %SearchBox
@onready var show_all_btn: CheckButton = %ShowAllBtn
@onready var node_tree: Tree = %NodeTree

func _init() -> void:
	hide()

func _ready() -> void:
	file_dialog = EditorFileDialog.new()
	file_dialog.access = EditorFileDialog.ACCESS_RESOURCES
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.tscn", "PackedScene")
	file_dialog.file_selected.connect(_on_file_selected)
	EditorInterface.get_base_control().call_deferred("add_child", file_dialog)

	confirmed.connect(_on_confirmed)
	canceled.connect(_clean_up_scene)
	about_to_popup.connect(func(): search_box.call_deferred("grab_focus"))
	
	change_scene_btn.pressed.connect(func(): hide(); file_dialog.popup_file_dialog())
	
	search_box.text_changed.connect(func(_t): _trigger_tree_rebuild())
	show_all_btn.toggled.connect(func(_t): _trigger_tree_rebuild())
	node_tree.item_activated.connect(_on_confirmed)


func setup_and_open(scene_path: String, config: FilterConfig) -> void:
	allowed_class_lbl.text = config.target_class
	
	allowed_icon.texture = EditorIconUtils.resolve_icon(config.icon, config.custom_script, config.target_class)
	
	node_tree.configure(config)
	
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
		_trigger_tree_rebuild()

func _trigger_tree_rebuild() -> void:
	node_tree.rebuild(temp_scene_instance, search_box.text, show_all_btn.button_pressed)

func _on_file_selected(path: String) -> void:
	var uid: int = ResourceLoader.get_resource_uid(path)
	current_scene_path = ResourceUID.id_to_text(uid) if uid != ResourceUID.INVALID_ID else path
	_load_scene(path)
	call_deferred("popup_centered")

func _on_confirmed() -> void:
	var selected: TreeItem = node_tree.get_selected()
	if selected:
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
	node_tree.clear()
