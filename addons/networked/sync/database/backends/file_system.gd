## [NetwBackend] that stores records as [DictionaryEntity] files on disk.
##
## Records are written to [code]<base_dir>/<table>/<id><extension>[/code].
## By default files use the compact binary [code].dict[/code] format.
## Set [member use_text_format] to [code]true[/code] to write human-readable
## [code].tdict[/code] JSON instead (useful during development).
##
## [b]Ghost-table detection:[/b] on [method initialize] any subdirectory found
## under [member base_dir] that is [i]not[/i] present in the supplied schema is
## reported as a ghost table via [method @GlobalScope.push_warning]. No data is
## ever deleted automatically.
class_name FileSystemBackend
extends NetwBackend


# Static registry mapping globalized base_dir -> WeakRef(FileSystemBackend).
# Used to catch multiple instances trying to manage the same directory.
static var _path_registry: Dictionary = {}


# Clears the static registry of active backends.
# Internal: used by testing infrastructure to prevent cross-test leakage.
static func _clear_path_registry() -> void:
	_path_registry.clear()


## Root directory for all table subdirectories.
## In exported builds you should point this at [code]user://saves[/code].
@export_dir var base_dir: String = "res://saves"

## When [code]true[/code], files are written as readable [code].tdict[/code] JSON.
## When [code]false[/code] (default), files use compact binary [code].dict[/code].
@export var use_text_format: bool = false


# Returns the file extension based on the current format setting.
func _get_extension() -> String:
	return ".tdict" if use_text_format else ".dict"


# Returns the full absolute path for a specific record.
func _path_for(table: StringName, id: StringName) -> String:
	return base_dir.path_join(String(table)).path_join(String(id) + _get_extension())


# Returns the absolute path to a table's subdirectory.
func _table_dir(table: StringName) -> String:
	return base_dir.path_join(String(table))


# ── NetwBackend overrides ────────────────────────────────────────────────

# Initializes the storage directory and validates the schema.
func initialize(schema: Dictionary) -> Error:
	# Purge stale entries from the registry (backends that have been freed).
	var stale_paths: Array = []
	for path in _path_registry:
		if not _path_registry[path].get_ref():
			stale_paths.append(path)
	for path in stale_paths:
		_path_registry.erase(path)
	
	# Ensure the same base_dir isn't being used by multiple active backends.
	var global_path := ProjectSettings.globalize_path(base_dir)
	if _path_registry.has(global_path):
		var other = _path_registry[global_path].get_ref()
		if other and other != self:
			assert(false, 
				"FileSystemBackend: Multiple instances are pointing to the " + \
				"same base_dir '%s'. This will cause data corruption and " % [base_dir] + \
				"ghost-table warnings. Consider using a shared NetwDatabase " + \
				"resource or a different base_dir if they are conceptually " + \
				"different databases."
			)
	_path_registry[global_path] = weakref(self)
	
	# Redirect res:// to user:// in exported builds.
	if not OS.has_feature("editor") and base_dir.begins_with("res://"):
		base_dir = base_dir.replace("res://", "user://")
	
	# Ensure the root directory exists.
	if not DirAccess.dir_exists_absolute(base_dir):
		var err := DirAccess.make_dir_recursive_absolute(base_dir)
		if err != OK:
			Netw.dbg.error(
				"FileSystemBackend: could not create base_dir '%s'. Error: %s",
				[base_dir, error_string(err)],
				func(m): push_error(m)
			)
			return err
	
	# Create subdirectories for all known tables.
	for table: StringName in schema:
		var table_dir := _table_dir(table)
		if not DirAccess.dir_exists_absolute(table_dir):
			DirAccess.make_dir_recursive_absolute(table_dir)
	
	# Report ghost tables (subdirs on disk not in schema).
	var dir := DirAccess.open(base_dir)
	if dir:
		dir.list_dir_begin()
		var entry := dir.get_next()
		while entry != "":
			if dir.current_is_dir() and not entry.begins_with("."):
				var sn := StringName(entry)
				if not schema.has(sn):
					Netw.dbg.warn(
						"FileSystemBackend: ghost table '%s' found at '%s'. " \
						+ "It is not in the current schema. Run a manual migration " \
						+ "or delete the directory if it is no longer needed.",
						[entry, base_dir.path_join(entry)],
						func(m): push_warning(m)
					)
			entry = dir.get_next()
		dir.list_dir_end()
	
	return OK


# Writes a record to disk, merging with existing data.
func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	var path := _path_for(table, id)
	
	# Ensure the table directory exists (may have been added after initialize).
	var table_dir := _table_dir(table)
	if not DirAccess.dir_exists_absolute(table_dir):
		DirAccess.make_dir_recursive_absolute(table_dir)
	
	# Load existing record and merge so we never clobber untracked columns.
	var record := DictionaryEntity.new()
	if ResourceLoader.exists(path):
		var existing := ResourceLoader.load(path, "DictionaryEntity", ResourceLoader.CACHE_MODE_REPLACE)
		if existing is DictionaryEntity:
			record.data = existing.data.duplicate()
	
	for key: StringName in data:
		record.data[key] = data[key]
	
	return ResourceSaver.save(record, path)


# Reads a record from disk.
func find_by_id(table: StringName, id: StringName) -> Dictionary:
	var path := _path_for(table, id)
	if not ResourceLoader.exists(path):
		return {}
	var res := ResourceLoader.load(path, "DictionaryEntity", ResourceLoader.CACHE_MODE_REPLACE)
	if res is DictionaryEntity:
		return res.data.duplicate()
	return {}


# Returns all matching records for a table.
func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
	var table_dir := _table_dir(table)
	if not DirAccess.dir_exists_absolute(table_dir):
		return []
	
	var results: Array[Dictionary] = []
	var ext := _get_extension()
	
	var dir := DirAccess.open(table_dir)
	if not dir:
		return []
	
	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if not dir.current_is_dir() and entry.ends_with(ext):
			var path := table_dir.path_join(entry)
			var res := ResourceLoader.load(path, "DictionaryEntity", ResourceLoader.CACHE_MODE_REPLACE)
			if res is DictionaryEntity:
				var record: Dictionary = res.data
				if _matches_filter(record, filter):
					results.append(record.duplicate())
		entry = dir.get_next()
	dir.list_dir_end()
	
	return results


# Permanently removes a record from disk.
func delete(table: StringName, id: StringName) -> Error:
	var path := _path_for(table, id)
	if not ResourceLoader.exists(path):
		return OK
	return DirAccess.remove_absolute(path)


# ── Helpers ───────────────────────────────────────────────────────────────────

# Returns true if the record matches the provided filter dictionary.
func _matches_filter(record: Dictionary, filter: Dictionary) -> bool:
	for key: StringName in filter:
		if not record.has(key) or record[key] != filter[key]:
			return false
	return true
