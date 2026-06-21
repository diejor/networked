## Persisted list of [JoinTarget]s shown by the server browser.
##
## Saved as a [code].tres[/code] under [code]user://[/code] so it
## survives across sessions. Use [method load_or_new] to read and
## [method save] to write. The resource itself is just a typed array
## wrapper.
## [br][br]
## [codeblock]
## var list := ServerList.load_or_new("user://my_servers.tres")
## list.targets.append(new_target)
## ServerList.save(list, "user://my_servers.tres")
## [/codeblock]
class_name ServerList
extends Resource

const DEFAULT_PATH := "user://servers.tres"

## The saved targets, in display order.
@export var targets: Array[JoinTarget] = []


## Loads the server list from [param path], or returns a fresh empty
## list when the file does not exist (or fails to load).
static func load_or_new(path: String = DEFAULT_PATH) -> ServerList:
	if not FileAccess.file_exists(path):
		return ServerList.new()

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ServerList.new()

	# If the file begins with a bracket '[', it is a valid Godot text resource file.
	# Otherwise, it might be a binary or corrupted file, so we skip it to prevent engine errors.
	if file.get_length() > 0:
		var first_byte := file.get_8()
		while (
			(
				first_byte == 32
				or first_byte == 9
				or first_byte == 10
				or first_byte == 13
			)
			and file.get_position() < file.get_length()
		):
			first_byte = file.get_8()
		if first_byte != 91: # ASCII '['
			file.close()
			Netw.dbg.warn(
				"ServerList: File at %s is not a valid text resource. Skipping.",
				[path],
			)
			return ServerList.new()
		file.close()

	var res := ResourceLoader.load(path, "ServerList", ResourceLoader.CACHE_MODE_IGNORE)
	if res is ServerList:
		return res
	return ServerList.new()


## Writes [param list] to [param path]. Returns the
## [enum @GlobalScope.Error] from [method ResourceSaver.save].
static func save(list: ServerList, path: String = DEFAULT_PATH) -> Error:
	if list == null:
		return ERR_INVALID_PARAMETER
	var base_dir := path.get_base_dir()
	if not DirAccess.dir_exists_absolute(base_dir):
		var err := DirAccess.make_dir_recursive_absolute(base_dir)
		if err != OK:
			Netw.dbg.error(
				"ServerList: failed to create parent directory %s: %s.",
				[base_dir, error_string(err)],
			)
			return err
	return ResourceSaver.save(list, path)
