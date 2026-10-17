@tool
extends EditorExportPlugin

var excluded: PackedStringArray
var caches: Dictionary[String, PackedByteArray]
var cache_root: String


func _get_name() -> String:
	return "NetworkedVendoredExtensions"


func _export_begin(
		features: PackedStringArray,
		_debug: bool,
		_path: String,
		_flags: int,
) -> void:
	excluded.clear()
	for directory: String in ["res://addons/godotsteam/", "res://addons/webrtc/"]:
		var manifest := ConfigFile.new()
		if manifest.load(directory + directory.get_slice("/", 3) + ".gdextension") != OK:
			continue
		var supported := false
		for key: String in manifest.get_section_keys("libraries"):
			var matches := true
			for feature: String in key.split("."):
				matches = matches and features.has(feature)
			supported = supported or matches
		if not supported:
			excluded.append(directory)

	if excluded.has("res://addons/godotsteam/"):
		excluded.append("res://addons/networked_steam/")
	if excluded.is_empty():
		return
	cache_root = "res://.godot/" if ProjectSettings.get_setting(
		"application/config/use_hidden_project_data_directory",
		true,
	) else "res://godot/"
	for cache: String in ["extension_list.cfg", "global_script_class_cache.cfg"]:
		var path := cache_root + cache
		caches[path] = FileAccess.get_file_as_bytes(path)
	var extensions := PackedStringArray()
	for path: String in FileAccess.get_file_as_string(
		cache_root + "extension_list.cfg",
	).split("\n", false):
		var keep := true
		for directory: String in excluded:
			keep = keep and not path.begins_with(directory)
		if keep:
			extensions.append(path)
	FileAccess.open(cache_root + "extension_list.cfg", FileAccess.WRITE) \
			.store_string("\n".join(extensions) + "\n")
	var classes := ConfigFile.new()
	classes.load(cache_root + "global_script_class_cache.cfg")
	var kept: Array = []
	for entry: Dictionary in classes.get_value("", "list", []):
		var keep := true
		for directory: String in excluded:
			keep = keep and not String(entry.path).begins_with(directory)
		if keep:
			kept.append(entry)
	classes.set_value("", "list", kept)
	classes.save(cache_root + "global_script_class_cache.cfg")


func _export_end() -> void:
	for path: String in caches:
		FileAccess.open(path, FileAccess.WRITE).store_buffer(caches[path])
	caches.clear()


func _export_file(path: String, _type: String, _features: PackedStringArray) -> void:
	for directory: String in excluded:
		if path.begins_with(directory):
			skip()
