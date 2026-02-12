@tool
extends EditorPlugin


const SETTING_PATH = "networked/config"
const DEFAULT_PATH = "res://networked_config.tres"

func _enter_tree():
	if not ProjectSettings.has_setting(SETTING_PATH):
		ProjectSettings.set_setting(SETTING_PATH, DEFAULT_PATH)
	
	ProjectSettings.add_property_info({
		"name": SETTING_PATH,
		"type": TYPE_STRING,
		"hint": PROPERTY_HINT_FILE,
		"hint_string": "*.tres,*.res" 
	})
	
	ProjectSettings.save()


func _exit_tree() -> void:
	if ProjectSettings.has_setting(SETTING_PATH):
		ProjectSettings.set_setting(SETTING_PATH, null)
