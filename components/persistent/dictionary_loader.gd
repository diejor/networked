@tool
## ResourceFormatLoader for DictionarySave.
## - .tdict: loads JSON into DictionarySave.
## - .dict:  loads store_var() binary into DictionarySave.
class_name DictionarySaveFormatLoader
extends ResourceFormatLoader

const TEXT_EXT := "tdict"
const BIN_EXT  := "dict"


func _get_recognized_extensions() -> PackedStringArray:
	return PackedStringArray([TEXT_EXT, BIN_EXT])


func _handles_type(type: StringName) -> bool:
	return (type == &"Resource"
		or type == &"SaveContainer"
		or type == &"DictionarySave")


func _get_resource_type(path: String) -> String:
	var ext := path.get_extension().to_lower()
	if ext == TEXT_EXT or ext == BIN_EXT:
		return "DictionarySave"
	return ""


func _recognize_path(path: String, type: StringName) -> bool:
	var ext := path.get_extension().to_lower()
	if ext != TEXT_EXT and ext != BIN_EXT:
		return false

	if type != StringName():
		return _handles_type(type)

	return true


func _exists(path: String) -> bool:
	return FileAccess.file_exists(path)


func _load(path: String, _original_path: String, _use_sub_threads: bool, _cache_mode: int) -> Variant:
	var ext := path.get_extension().to_lower()
	assert(ext == TEXT_EXT or ext == BIN_EXT, 
		"`dictionary_loader` given unsupported extension: %s." % ext)

	if not FileAccess.file_exists(path):
		return ERR_FILE_NOT_FOUND

	var file := FileAccess.open(path, FileAccess.READ)
	assert(file != null, "Failed to open file in `DictionaryRFL.`")
	if file == null:
		return FileAccess.get_open_error()

	var dict_res := DictionarySave.new()

	match ext:
		TEXT_EXT:
			var text := file.get_as_text()

			var json := JSON.new()
			var err := json.parse(text)
			assert(err == OK, 
				"JSON parse failed for `DictionarySave` `\"*.tdict\"`. 
				Error: %s" % error_string(err)) 

			dict_res.data = JSON.to_native(json.data, false)
		BIN_EXT:
			assert(ext == BIN_EXT)
			if file.get_length() == 0:
				return ERR_FILE_CORRUPT

			var decoded: Variant = file.get_var()
			assert(typeof(decoded) == TYPE_DICTIONARY, 
				"Binary `\"*.dict\"` did not contain a Dictionary for 
				DictionarySave.")
			if typeof(decoded) != TYPE_DICTIONARY:
				return ERR_FILE_CORRUPT

			dict_res.data = decoded
		_:
			@warning_ignore("assert_always_false")
			assert(false, "Unrecognized extension.")
		
	
	dict_res.take_over_path(path)
	return dict_res
