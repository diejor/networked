@tool
extends EditorPlugin

const LIB := "res://addons/discord_social_sdk/lib/"
const MANIFEST := LIB + "discord.gdextension"
const SDK_VERSION := "1.10.19337"
const LIBRARY := {
	"Linux": "linux/libdiscord_partner_sdk.so",
	"Windows": "windows/discord_partner_sdk.dll",
}


func _enter_tree() -> void:
	if FileAccess.file_exists(MANIFEST) or not LIBRARY.has(OS.get_name()):
		return
	var library: String = LIB + LIBRARY[OS.get_name()]
	var file := library.get_file()
	if not FileAccess.file_exists(library):
		push_warning(
			(
					"Discord's library %s is missing, so Discord features "
					+ "are off. Download Discord Social SDK %s from the "
					+ "Discord Developer Portal and copy %s into %s."
			) % [file, SDK_VERSION, file, library.get_base_dir()],
		)
		return

	DirAccess.copy_absolute(MANIFEST + ".template", MANIFEST)
	if GDExtensionManager.load_extension(MANIFEST) \
			!= GDExtensionManager.LOAD_STATUS_OK:
		DirAccess.remove_absolute(MANIFEST)
		push_warning(
			(
					"Discord's library %s did not load, so Discord features "
					+ "are off. Read the error above, and on Linux check "
					+ "that glibc is 2.38 or newer."
			) % file,
		)
		return

	var list := EditorInterface.get_editor_paths().get_project_settings_dir() \
			.get_base_dir().path_join("extension_list.cfg")
	var listed := FileAccess.get_file_as_string(list)
	FileAccess.open(list, FileAccess.WRITE).store_string(
		listed + MANIFEST + "\n",
	)
