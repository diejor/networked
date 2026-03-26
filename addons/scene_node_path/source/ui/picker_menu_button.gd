@tool
extends MenuButton

signal action_requested(action_id: int)

func _enter_tree() -> void:
	var popup: PopupMenu = get_popup()
	popup.clear() 
	
	popup.add_item("Change Scene", 0)
	popup.add_item("Copy as UID", 1)
	popup.add_item("Copy as Path", 2)
	popup.add_separator()
	popup.add_item("Clear", 4)
	
	if not popup.id_pressed.is_connected(_on_popup_id_pressed):
		popup.id_pressed.connect(_on_popup_id_pressed)
	
	if Engine.is_editor_hint():
		var editor_theme: Theme = EditorInterface.get_editor_theme()
		
		icon = editor_theme.get_icon("GuiTabMenuHl", "EditorIcons")
		popup.set_item_icon(0, editor_theme.get_icon("Load", "EditorIcons"))
		popup.set_item_icon(1, editor_theme.get_icon("UID", "EditorIcons"))
		popup.set_item_icon(2, editor_theme.get_icon("NodePath", "EditorIcons"))
		popup.set_item_icon(4, editor_theme.get_icon("Clear", "EditorIcons"))

func _on_popup_id_pressed(id: int) -> void:
	action_requested.emit(id)
