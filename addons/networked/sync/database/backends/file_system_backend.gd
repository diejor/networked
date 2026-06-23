## [NetwDatabaseBackend] that stores each record as one [DictionaryRecord] file.
##
## A record lives at [code]<root>/<table>/<id>.<ext>[/code], where the root folds
## [member app_id] and the save slot into [member base_dir] so two slots never
## share a directory. Every read and write goes straight to disk through
## [ResourceLoader] and [ResourceSaver] with no cache, so
## [member NetwDatabase.warm_policy] is ignored and a write is durable the moment
## it returns.
## [codeblock]
## var db := NetwDatabase.new()
## var fs := FileSystemBackend.new()
## fs.base_dir = "user://saves"
## fs.use_text_format = true   # readable .tdict instead of binary .dict
## db.backend = fs
## [/codeblock]
##
## A subdirectory under the slot root that no registered table claims is a ghost
## table. [method initialize] reports each one through
## [method @GlobalScope.push_warning] and never deletes data on its own.
class_name FileSystemBackend
extends NetwDatabaseBackend

# Static registry mapping globalized base_dir -> WeakRef(FileSystemBackend).
# Used to catch multiple instances trying to manage the same directory.
static var _path_registry: Dictionary = { }


# Clears the static registry of active backends.
# Internal: used by testing infrastructure to prevent cross-test leakage.
static func _clear_path_registry() -> void:
	_path_registry.clear()

## Root directory for all table subdirectories.
## In exported builds you should point this at [code]user://saves[/code].
@export_dir var base_dir: String = "res://saves"

## Application scope folded into the storage path ahead of the save slot.
## Leave empty to store slots directly under [member base_dir]. Set it to keep
## several games sharing one [member base_dir] from colliding.
@export var app_id: String = ""

## Picks the record file extension. When [code]true[/code] files are the readable
## JSON [constant DictionaryRecordFormatSaver.TEXT_EXT]. When [code]false[/code]
## (default) they are the compact binary [constant DictionaryRecordFormatSaver.BIN_EXT].
@export var use_text_format: bool = false

# Effective root after [method initialize] folds app_id and the save slot into
# base_dir. Every record path resolves under this, never base_dir directly.
var _root: String = ""


# Returns the file extension based on the current format setting.
func _get_extension() -> String:
	return ".tdict" if use_text_format else ".dict"


# Composes base_dir with the app_id segment, the shared parent of every slot.
func _app_dir() -> String:
	if app_id.is_empty():
		return base_dir
	return base_dir.path_join(app_id)


# Composes the effective root for a save slot. An empty slot stores directly
# under the app dir, preserving the pre-slot layout.
func _root_for(slot: String) -> String:
	if slot.is_empty():
		return _app_dir()
	return _app_dir().path_join(slot)


# Returns the slot root, falling back to base_dir when initialize has not run.
func _active_root() -> String:
	return _root if not _root.is_empty() else base_dir


# Returns the full absolute path for a specific record.
func _path_for(table: StringName, id: StringName) -> String:
	return _active_root().path_join(String(table)).path_join(String(id) + _get_extension())


# Returns the absolute path to a table's subdirectory.
func _table_dir(table: StringName) -> String:
	return _active_root().path_join(String(table))

# ── NetwDatabaseBackend overrides ────────────────────────────────────────


## Overrides [method NetwDatabaseBackend.initialize] to fold [param slot] into the
## storage root, create one subdirectory per schema table, and warn on any
## ghost-table directory the schema no longer claims.
func initialize(schema: Dictionary, slot: String = "") -> Error:
	# Redirect res:// to user:// in exported builds.
	if not OS.has_feature("editor") and base_dir.begins_with("res://"):
		base_dir = base_dir.replace("res://", "user://")

	# Fold app_id and the save slot into the effective root before any path use.
	_root = _root_for(slot)

	# Purge stale entries from the registry (backends that have been freed).
	var stale_paths: Array = []
	for path in _path_registry:
		if not _path_registry[path].get_ref():
			stale_paths.append(path)
	for path in stale_paths:
		_path_registry.erase(path)

	# Ensure the same slot root isn't being used by multiple active backends.
	# Keying on the slot root (not base_dir) lets two slots coexist.
	var global_path := ProjectSettings.globalize_path(_root)
	if _path_registry.has(global_path):
		var other = _path_registry[global_path].get_ref()
		if other and other != self:
			assert(
				false,
				"FileSystemBackend: Multiple instances are pointing to the " +
				"same slot root '%s'. This will cause data corruption and " % [_root] +
				"ghost-table warnings. Consider using a shared NetwDatabase " +
				"resource or a different base_dir if they are conceptually " +
				"different databases.",
			)
	_path_registry[global_path] = weakref(self)

	# Ensure the root directory exists.
	if not DirAccess.dir_exists_absolute(_root):
		var err := DirAccess.make_dir_recursive_absolute(_root)
		if err != OK:
			Netw.dbg.error(
				"FileSystemBackend: could not create slot root '%s'. Error: %s",
				[_root, error_string(err)],
				func(m): push_error(m)
			)
			return err

	# Create subdirectories for all known tables.
	for table: StringName in schema:
		var table_dir := _table_dir(table)
		if not DirAccess.dir_exists_absolute(table_dir):
			DirAccess.make_dir_recursive_absolute(table_dir)

	# Report ghost tables (subdirs on disk not in schema).
	var dir := DirAccess.open(_root)
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
						[entry, _root.path_join(entry)],
						func(m): push_warning(m)
					)
			entry = dir.get_next()
		dir.list_dir_end()

	return OK


## Overrides [method NetwDatabaseBackend.list_namespaces] to list the save slots
## (subdirectories) present under the application directory.
func list_namespaces() -> Array[StringName]:
	var out: Array[StringName] = []
	var app_dir := _app_dir()
	var dir := DirAccess.open(app_dir)
	if not dir:
		return out
	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if dir.current_is_dir() and not entry.begins_with("."):
			out.append(StringName(entry))
		entry = dir.get_next()
	dir.list_dir_end()
	return out


## Overrides [method NetwDatabaseBackend.delete_namespace] to recursively remove
## the directory and all record files under [param slot].
func delete_namespace(slot: String) -> Error:
	if slot.is_empty():
		return ERR_INVALID_PARAMETER
	var slot_root := _root_for(slot)
	if not DirAccess.dir_exists_absolute(slot_root):
		return OK
	return _remove_recursive(slot_root)


# Deletes a directory tree (files then subdirectories, depth first).
func _remove_recursive(path: String) -> Error:
	var dir := DirAccess.open(path)
	if not dir:
		return ERR_CANT_OPEN
	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		var child := path.path_join(entry)
		if dir.current_is_dir():
			var err := _remove_recursive(child)
			if err != OK:
				return err
		else:
			var err := DirAccess.remove_absolute(child)
			if err != OK:
				return err
		entry = dir.get_next()
	dir.list_dir_end()
	return DirAccess.remove_absolute(path)


## Overrides [method NetwDatabaseBackend.upsert] to write a record to disk,
## merging the new fields with the existing file's contents.
func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	var path := _path_for(table, id)

	# Ensure the table directory exists (may have been added after initialize).
	var table_dir := _table_dir(table)
	if not DirAccess.dir_exists_absolute(table_dir):
		DirAccess.make_dir_recursive_absolute(table_dir)

	# Load existing record and merge so we never clobber untracked columns.
	var record := DictionaryRecord.new()
	if ResourceLoader.exists(path):
		var existing := ResourceLoader.load(path, "DictionaryRecord", ResourceLoader.CACHE_MODE_REPLACE)
		if existing is DictionaryRecord:
			record.data = existing.data.duplicate()

	for key: StringName in data:
		record.data[key] = data[key]

	return ResourceSaver.save(record, path)


## Overrides [method NetwDatabaseBackend.find_by_id] to load and return a record
## from its file on disk, or an empty dictionary if it does not exist.
func find_by_id(table: StringName, id: StringName) -> Dictionary:
	var path := _path_for(table, id)
	if not ResourceLoader.exists(path):
		return { }
	var res := ResourceLoader.load(path, "DictionaryRecord", ResourceLoader.CACHE_MODE_REPLACE)
	if res is DictionaryRecord:
		return res.data.duplicate()
	return { }


## Overrides [method NetwDatabaseBackend.find_all] to load all files in the
## table's directory and return those that match [param filter].
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
			var res := ResourceLoader.load(path, "DictionaryRecord", ResourceLoader.CACHE_MODE_REPLACE)
			if res is DictionaryRecord:
				var record: Dictionary = res.data
				if _matches_filter(record, filter):
					results.append(record.duplicate())
		entry = dir.get_next()
	dir.list_dir_end()

	return results


## Overrides [method NetwDatabaseBackend.delete] to permanently remove a
## record's file from disk.
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
