@tool
extends EditorExportPlugin

## Maps each optional GDExtension manifest to the addon directories and
## scripts that need it. An export with no installed library for a manifest
## omits the binding's addon directory and these paths. It also omits every
## script that names a class of the binding or of an omitted script, and
## exported scenes lose the nodes that use an omitted script. A listed path
## stays out even when the editor itself cannot load the binding.
const OPTIONAL := {
	"res://addons/godotsteam/godotsteam.gdextension": [
		"res://addons/networked_steam/",
	],
	"res://addons/webrtc/webrtc.gdextension": [],
	"res://addons/discord_social_sdk/lib/discord.gdextension": [
		"res://addons/networked_discord/discord_social.gd",
		"res://addons/networked_discord/discord_party.gd",
		"res://addons/networked_discord/ui/",
	],
}
## Maps each optional GDExtension manifest to the prefix of every class its
## library registers.
const CLASS_PREFIX := {
	"res://addons/godotsteam/godotsteam.gdextension": "Steam",
	"res://addons/webrtc/webrtc.gdextension": "WebRTCLib",
	"res://addons/discord_social_sdk/lib/discord.gdextension": "Discord",
}

var excluded: PackedStringArray
var caches: Dictionary[String, PackedByteArray]


# Godot runs export plugins in name order, and a file the GDExtension or
# GDScript plugin already exported cannot be skipped, so this name sorts first.
func _get_name() -> String:
	return "Bindings"


func _export_begin(
		features: PackedStringArray,
		_debug: bool,
		_path: String,
		_flags: int,
) -> void:
	excluded.clear()
	var names := PackedStringArray()
	for path: String in OPTIONAL:
		var manifest := ConfigFile.new()
		var keys: PackedStringArray = (
				manifest.get_section_keys("libraries")
				if manifest.load(path) == OK
				else PackedStringArray()
		)
		var supported := false
		for key: String in keys:
			var library: String = manifest.get_value("libraries", key)
			var file := (
					path.get_base_dir().path_join(library)
					if library.is_relative_path()
					else library
			)
			var matches := FileAccess.file_exists(file) \
					or DirAccess.dir_exists_absolute(file)
			for feature: String in key.split("."):
				matches = matches and features.has(feature)
			supported = supported or matches
		if not supported:
			excluded.append("res://addons/%s/" % path.get_slice("/", 3))
			excluded.append_array(OPTIONAL[path])
			for name: String in ClassDB.get_class_list():
				if ClassDB.class_get_api_type(name) == ClassDB.API_EXTENSION \
						and name.begins_with(CLASS_PREFIX[path]):
					names.append(name)
	if excluded.is_empty():
		return
	var cache_root := "res://.godot/" if ProjectSettings.get_setting(
		"application/config/use_hidden_project_data_directory",
		true,
	) else "res://godot/"
	# Godot 4.7 keys its exported scene cache without
	# _get_customization_configuration_hash, so it would reuse a scene
	# stripped for another set of bindings.
	for directory: String in DirAccess.get_directories_at(
		cache_root + "exported",
	):
		DirAccess.remove_absolute(
			cache_root + "exported/%s/file_cache" % directory,
		)
	for cache: String in [
		"extension_list.cfg",
		"global_script_class_cache.cfg",
	]:
		var path := cache_root + cache
		caches[path] = FileAccess.get_file_as_bytes(path)
	var extensions := PackedStringArray()
	for path: String in FileAccess.get_file_as_string(
		cache_root + "extension_list.cfg",
	).split("\n", false):
		if not is_excluded(path):
			extensions.append(path)
	FileAccess.open(cache_root + "extension_list.cfg", FileAccess.WRITE) \
			.store_string("\n".join(extensions) + "\n")
	var classes := ConfigFile.new()
	classes.load(cache_root + "global_script_class_cache.cfg")
	var class_of: Dictionary[String, String] = { }
	for entry: Dictionary in classes.get_value("", "list", []):
		class_of[entry.path] = str(entry.class)
		if is_excluded(entry.path):
			names.append(entry.class)

	# GDScript reports no dependency on a class it names, so the scan reads
	# identifiers from source and skips strings and comments.
	var tokens := RegEx.create_from_string(
		"\"\"\"[\\s\\S]*?\"\"\"|'''[\\s\\S]*?'''|\"(?:\\\\.|[^\"\\\\\\n])*\""
		+ "|'(?:\\\\.|[^'\\\\\\n])*'|#.*|(?<name>[A-Za-z_]\\w*)",
	)
	var identifiers: Dictionary[String, Dictionary] = { }
	var folders: Array[EditorFileSystemDirectory] = [
		EditorInterface.get_resource_filesystem().get_filesystem(),
	]
	while not folders.is_empty():
		var folder: EditorFileSystemDirectory = folders.pop_back()
		for index: int in folder.get_subdir_count():
			folders.append(folder.get_subdir(index))
		for index: int in folder.get_file_count():
			var path := folder.get_file_path(index)
			if folder.get_file_type(index) != &"GDScript" \
					or is_excluded(path):
				continue
			var found := { }
			for token: RegExMatch in tokens.search_all(
				FileAccess.get_file_as_string(path),
			):
				found[token.get_string("name")] = true
			identifiers[path] = found
	var grew := true
	while grew:
		grew = false
		for path: String in identifiers.keys():
			for name: String in names:
				if identifiers[path].has(name):
					excluded.append(path)
					if class_of.has(path):
						names.append(class_of[path])
					identifiers.erase(path)
					grew = true
					break

	var listed: Array = []
	for entry: Dictionary in classes.get_value("", "list", []):
		if not is_excluded(entry.path):
			listed.append(entry)
	classes.set_value("", "list", listed)
	classes.save(cache_root + "global_script_class_cache.cfg")


func _export_end() -> void:
	for path: String in caches:
		FileAccess.open(path, FileAccess.WRITE).store_buffer(caches[path])
	caches.clear()


func _export_file(
		path: String,
		_type: String,
		_features: PackedStringArray,
) -> void:
	if is_excluded(path):
		skip()


func _begin_customize_scenes(
		_platform: EditorExportPlatform,
		_features: PackedStringArray,
) -> bool:
	return not excluded.is_empty()


func _get_customization_configuration_hash() -> int:
	return hash(excluded)


func _customize_scene(scene: Node, _path: String) -> Node:
	var nodes: Array[Node] = [scene]
	nodes.append_array(scene.find_children("*", "", true, false))
	for node: Node in nodes:
		if not is_instance_valid(node) or node.get_script() == null \
				or not is_excluded(node.get_script().resource_path):
			continue
		if node == scene:
			node.set_script(null)
			continue
		node.get_parent().remove_child(node)
		node.free()
	return scene


## Returns whether this export omits [param path].
func is_excluded(path: String) -> bool:
	for prefix: String in excluded:
		if path.begins_with(prefix):
			return true
	return false
