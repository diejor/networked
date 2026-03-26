@tool
class_name PropertyPickerUI
extends HBoxContainer

signal open_picker_requested
signal copy_uid_requested
signal copy_path_requested
signal clear_requested

@onready var main_btn: Button = %MainBtn
@onready var menu_btn: MenuButton = %MenuBtn

func _ready() -> void:
	main_btn.pressed.connect(func(): open_picker_requested.emit())
	menu_btn.get_popup().id_pressed.connect(_on_menu_id_pressed)

func set_empty_state() -> void:
	main_btn.text = "Assign Scene..."
	main_btn.icon = null
	main_btn.remove_theme_color_override("font_color")

func set_broken_state(display_text: String, warning_color: Color, warning_icon: Texture2D) -> void:
	main_btn.text = display_text
	main_btn.icon = warning_icon
	main_btn.add_theme_color_override("font_color", warning_color)

func set_valid_state(display_text: String, icon: Texture2D) -> void:
	main_btn.text = display_text
	main_btn.icon = icon
	main_btn.remove_theme_color_override("font_color")

func _on_menu_id_pressed(id: int) -> void:
	match id:
		0: open_picker_requested.emit()
		1: copy_uid_requested.emit()
		2: copy_path_requested.emit()
		4: clear_requested.emit()
