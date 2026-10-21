@tool
extends EditorPlugin


func _enter_tree() -> void:
	if not ClassDB.class_exists(&"DiscordClient"):
		push_warning(
			"Networked Discord needs the Discord Social SDK, which is not "
			+ "installed. Install it as addons/networked_discord/README.md "
			+ "describes.",
		)
