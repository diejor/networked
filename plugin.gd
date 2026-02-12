@tool
extends EditorPlugin


func _enter_tree():
	if not ProjectSettings.has_setting(Networked.SETTING_PATH):
		ProjectSettings.set_setting(Networked.SETTING_PATH, Networked.DEFAULT_PATH)
	
	ProjectSettings.add_property_info({
		"name": Networked.SETTING_PATH,
		"type": TYPE_STRING,
		"hint": PROPERTY_HINT_FILE,
		"hint_string": "*.tres,*.res" 
	})
	
	ProjectSettings.save()


func _exit_tree() -> void:
	if ProjectSettings.has_setting(Networked.SETTING_PATH):
		ProjectSettings.set_setting(Networked.SETTING_PATH, null)
