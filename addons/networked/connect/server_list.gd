## Persisted list of [JoinTarget]s shown by the server browser.
##
## Saved as a [code].tres[/code] under [code]user://[/code] so it
## survives across sessions. Use [method load_or_new] to read and
## [method save] to write. The resource itself is just a typed array
## wrapper.
class_name ServerList
extends Resource

const DEFAULT_PATH := "user://servers.tres"

## The saved targets, in display order.
@export var targets: Array[JoinTarget] = []


## Loads the server list from [param path], or returns a fresh empty
## list when the file does not exist (or fails to load).
static func load_or_new(path: String = DEFAULT_PATH) -> ServerList:
	if not ResourceLoader.exists(path):
		return ServerList.new()
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
