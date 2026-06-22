## [ResourceFormatSaver] that writes [DictionaryRecord] files.
##
## [DictionaryRecordFormatSaver] stores [DictionaryRecord] values as
## human-readable text or compact binary resources.
##
## [codeblock]
## var record := DictionaryRecord.new()
## record.set_value(&"health", 100)
## ResourceSaver.save(record, "user://players/alice.tdict")
## [/codeblock]
@tool
class_name DictionaryRecordFormatSaver
extends ResourceFormatSaver

const TEXT_EXT := "tdict"
const BIN_EXT := "dict"


func _recognize(resource: Resource) -> bool:
	return resource is DictionaryRecord


func _get_recognized_extensions(resource: Resource) -> PackedStringArray:
	if resource != null and not resource is DictionaryRecord:
		return PackedStringArray()
	return PackedStringArray([TEXT_EXT, BIN_EXT])


func _recognize_path(resource: Resource, path: String) -> bool:
	if resource != null and not resource is DictionaryRecord:
		return false
	var ext := path.get_extension().to_lower()
	return ext == TEXT_EXT or ext == BIN_EXT


func _save(resource: Resource, path: String, _flags: int) -> Error:
	var dict_res := resource as DictionaryRecord
	assert(
		dict_res != null,
		"`DictionaryRecordFormatSaver` can only save `DictionaryRecord`.",
	)
	if dict_res == null:
		return ERR_UNAVAILABLE

	var ext := path.get_extension().to_lower()

	match ext:
		TEXT_EXT:
			return _save_as_json(dict_res, path)
		BIN_EXT:
			return _save_as_binary(dict_res, path)
		_:
			assert(false, "Unsupported extension for DictionaryRecord: %s" % ext)
			return ERR_FILE_UNRECOGNIZED


func _save_as_json(dict_res: DictionaryRecord, path: String) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert(file != null, "Failed to open file for JSON save at path `%s`." % path)
	if file == null:
		return FileAccess.get_open_error()

	var wrapper: Variant = JSON.from_native(dict_res.data, false)
	var json_text: String = JSON.stringify(wrapper, "  ")

	file.store_string(json_text)
	return OK


func _save_as_binary(dict_res: DictionaryRecord, path: String) -> Error:
	var file := FileAccess.open(path, FileAccess.WRITE)
	assert(file != null, "Failed to open file for binary record save.")
	if file == null:
		return FileAccess.get_open_error()

	file.store_var(dict_res.data)
	return OK
