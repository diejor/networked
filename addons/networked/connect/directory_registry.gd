## Maps [StringName] ids to [LobbyDirectory] instances for the browser
## to look up.
##
## Browsers add the registry under their scene and register all available
## directories.
class_name DirectoryRegistry
extends Node

var _directories: Dictionary = { }


func _init() -> void:
	name = "DirectoryRegistry"


## Adds [param directory] under [param id]. Replaces any existing
## registration for the same id.
func register(id: StringName, directory: LobbyDirectory) -> void:
	if directory == null:
		return
	_directories[id] = directory


## Removes the directory registered under [param id], if any.
func unregister(id: StringName) -> void:
	_directories.erase(id)


## Returns the directory registered under [param id], or
## [code]null[/code] when none exists.
func get_directory(id: StringName) -> LobbyDirectory:
	return _directories.get(id, null)


## Returns the list of currently registered ids.
func list_directories() -> Array[StringName]:
	var out: Array[StringName] = []
	for key in _directories.keys():
		out.append(key)
	return out


## Returns [code]true[/code] when [param id] has a registered directory.
func has_directory(id: StringName) -> bool:
	return _directories.has(id)
