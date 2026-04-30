## [ResourceFormatSaver] that writes [DictionaryEntity] resources to custom file formats.
##
## Supports two extensions:
## - [code].tdict[/code] — human-readable JSON text (uses [method JSON.from_native] to preserve Godot types).
## - [code].dict[/code] — compact binary [code]store_var()[/code] format.
@tool
class_name DictionarySaveFormatSaver
extends ResourceFormatSaver

const TEXT_EXT := "tdict"
const BIN_EXT  := "dict"


func _recognize(resource: Resource) -> bool:
	return resource is DictionaryEntity


func _get_recognized_extensions(resource: Resource) -> PackedStringArray:
	assert(resource is DictionaryEntity)
	return PackedStringArray([TEXT_EXT, BIN_EXT])


func _recognize_path(resource: Resource, path: String) -> bool:
	assert(resource is DictionaryEntity)
	var ext := path.get_extension().to_lower()
	return ext == TEXT_EXT or ext == BIN_EXT


func _save(resource: Resource, path: String, _flags: int) -> Error:
	var dict_res := resource as DictionaryEntity
	assert(dict_res != null,
		"`DictionarySaver` can only save `DictionaryEntity`.")
	if dict_res == null:
		return ERR_UNAVAILABLE

	var ext := path.get_extension().to_lower()

	match ext:
		TEXT_EXT:
			return _save_as_json(dict_res, path)
		BIN_EXT:
			return _save_as_binary(dict_res, path)
		_:
			assert(false,
				"Unsupported extension for DictionaryEntity: %s" % ext)
			return ERR_FILE_UNRECOGNIZED


func _save_as_json(dict_res: DictionaryEntity, path: String) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert(file != null,
		"Failed to open file for JSON save at path `%s`." % path)
	if file == null:
		return FileAccess.get_open_error()

	var wrapper: Variant = JSON.from_native(dict_res.data, false)
	var json_text: String = JSON.stringify(wrapper, "  ")

	file.store_string(json_text)
	return OK


func _save_as_binary(dict_res: DictionaryEntity, path: String) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert(file != null, "Failed to open file for binary save in DictionarySaveFormatSaver.")
	if file == null:
		return FileAccess.get_open_error()

	file.store_var(dict_res.data)
	return OK
