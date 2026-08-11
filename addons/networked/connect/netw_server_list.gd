## Persisted list of [NetwConnectTarget]s a browser saved by hand.
##
## The list is pure data. It stores [NetwConnectTarget]s, which name a
## destination by scheme and address and carry no transport instance or embedded
## script, so a saved [code].tres[/code] survives an addon reshuffle that a
## backend-bearing list could not. [NetwServerBrowser] owns the live copy and
## reads and writes it through [method NetwServerBrowser.load_server_list] and
## [method NetwServerBrowser.save_server_list].
## [codeblock]
## var list := NetwServerList.load_or_new("user://my_servers.tres")
## list.targets.append(target)
## NetwServerList.save(list, "user://my_servers.tres")
## [/codeblock]
class_name NetwServerList
extends Resource

## Default path used by [method load_or_new] and [method save].
const DEFAULT_PATH := "user://netw_servers.tres"

## The saved targets, in display order.
@export var targets: Array[NetwConnectTarget] = []


## Loads the list from [param path], or returns a fresh empty list when the file
## is missing, unreadable, or not a text resource.
static func load_or_new(path: String = DEFAULT_PATH) -> NetwServerList:
	if not FileAccess.file_exists(path):
		return NetwServerList.new()

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return NetwServerList.new()

	# A text resource opens with '['. A binary or corrupt file would push engine
	# errors through ResourceLoader, so skip anything that does not, past leading
	# whitespace.
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
				"NetwServerList: %s is not a text resource. Skipping.",
				[path],
			)
			return NetwServerList.new()
		file.close()

	# A list saved under an older addon layout can reference scripts that no
	# longer exist. Loading it pushes unavoidable engine errors, so detect a
	# broken reference up front and start fresh instead.
	if _references_missing_resource(path):
		Netw.dbg.warn(
			"NetwServerList: %s references resources that no longer exist. "
			+ "Starting a fresh list.",
			[path],
		)
		return NetwServerList.new()

	var res := ResourceLoader.load(path, "NetwServerList", ResourceLoader.CACHE_MODE_IGNORE)
	if res is NetwServerList:
		return res
	return NetwServerList.new()


# Returns [code]true[/code] when the text resource at [param path] declares an
# [code]ext_resource[/code] whose file is gone, which a stale saved list from a
# previous addon layout does.
static func _references_missing_resource(path: String) -> bool:
	var text := FileAccess.get_file_as_string(path)
	if text.is_empty():
		return false
	for line in text.split("\n"):
		var trimmed := line.strip_edges()
		if not trimmed.begins_with("[ext_resource"):
			continue
		var key := "path=\""
		var start := trimmed.find(key)
		if start == -1:
			continue
		start += key.length()
		var end := trimmed.find("\"", start)
		if end == -1:
			continue
		var res_path := trimmed.substr(start, end - start)
		if res_path.begins_with("res://") and not FileAccess.file_exists(res_path):
			return true
	return false


## Writes [param list] to [param path]. Returns the
## [enum @GlobalScope.Error] from the storage write.
static func save(list: NetwServerList, path: String = DEFAULT_PATH) -> Error:
	if list == null:
		return ERR_INVALID_PARAMETER
	var base_dir := path.get_base_dir()
	if not DirAccess.dir_exists_absolute(base_dir):
		var err := DirAccess.make_dir_recursive_absolute(base_dir)
		if err != OK:
			Netw.dbg.error(
				"NetwServerList: failed to create %s: %s.",
				[base_dir, error_string(err)],
			)
			return err
	return ResourceSaver.save(list, path)
