## Abstract storage contract every [NetwDatabase] backend implements.
##
## [NetwDatabase] validates the schema and applies
## [enum NetwDatabase.SchemaMismatchPolicy] before it ever reaches a backend, so
## an implementation only does raw CRUD on opaque payloads. Table names and
## record ids are [StringName] for cheap equality, and every payload is a
## [Dictionary] keyed by column [StringName].
## [codeblock]
## NetwDatabaseBackend
##  ┠╴ NetwDatabaseBackend.Dict   in-memory mirror, for tests
##  ┠╴ FileSystemBackend          one DictionaryRecord file per record
##  ┖╴ NakamaDatabase             write-behind cache over Nakama storage
##
## # A concrete backend overrides the five @abstract methods:
## func initialize(schema: Dictionary, slot: String) -> Error
## func upsert(table: StringName, id: StringName, data: Dictionary) -> Error
## func find_by_id(table: StringName, id: StringName) -> Dictionary
## func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]
## func delete(table: StringName, id: StringName) -> Error
## [/codeblock]
##
## [method commit], [method warm], [method list_namespaces], and
## [method delete_namespace] ship with working defaults, so a synchronous backend
## inherits them and only a network backend overrides.
@abstract
class_name NetwDatabaseBackend
extends Resource

## Called once after all schemas have been registered via
## [method NetwDatabase.declare_table] or [method NetwDatabase.bind].
## Use this to create directories, open connections, or validate existing
## data on disk. [param schema] maps each table name to its declared column
## names.
##
## [param slot] scopes every record this backend touches under a save slot.
## It is composed into a storage prefix (a subdirectory, a collection prefix)
## and is never part of the record key, so [method find_all] stays a single
## listing. An empty slot selects the default save.
@abstract func initialize(schema: Dictionary, slot: String) -> Error


## Writes or updates a single record in [param table] identified by [param id].
##
## Only the keys present in [param data] are written; existing keys not present
## in [param data] are preserved (merge, not replace).
@abstract func upsert(table: StringName, id: StringName, data: Dictionary) -> Error


## Commits a batch of upsert [param operations] as one unit.
##
## Each operation is a [Dictionary] with [code]table[/code], [code]id[/code], and
## [code]data[/code] keys. The default loops [method upsert] in order and returns
## the first error. Network backends override this to coalesce the batch into one
## round trip. Returns [constant OK] on success.
func commit(operations: Array) -> Error:
	for entry in operations:
		var err := upsert(entry.table, entry.id, entry.data)
		if err != OK:
			return err
	return OK


## Returns the record for [param id] in [param table], or an empty [Dictionary]
## if no record exists.
@abstract func find_by_id(table: StringName, id: StringName) -> Dictionary


## Returns all records in [param table] that match every key/value pair in
## [param filter]. An empty [param filter] returns all records in the table.
@abstract func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]


## Permanently removes the record for [param id] from [param table].
## Returns [constant OK] even if the record did not exist (idempotent).
@abstract func delete(table: StringName, id: StringName) -> Error


## Pre-loads records into the backend's cache according to [param directives].
##
## Each directive is a [Dictionary] with a [code]table[/code] [StringName] and a
## [code]request[/code] [WarmRequest] describing what to fetch. Synchronous
## backends have no cache and no-op. Write-behind backends use this as their
## readiness gate. Returns [constant OK] by default.
func warm(_directives: Array) -> Error:
	return OK


## Returns every save-slot namespace this backend currently holds.
##
## Powers a save-select menu before any slot is open. The default backend keeps
## no namespaces and returns an empty array.
func list_namespaces() -> Array[StringName]:
	return [] as Array[StringName]


## Permanently removes every record under [param slot]. Idempotent.
## The default backend has no namespaces and returns [constant OK].
func delete_namespace(_slot: String) -> Error:
	return OK


## In-memory [NetwDatabaseBackend] backed by a nested [Dictionary].
##
## The whole store lives in [code]namespace -> table -> id -> Dictionary[/code]
## and vanishes when the backend is freed, so it is what a unit test for
## [SaveComponent] or [NetwDatabase] reaches for when disk I/O would only add
## flakiness. It overrides every method, so warming never applies.
## [codeblock]
## var db := NetwDatabase.new()
## db.backend = NetwDatabaseBackend.Dict.new()
## db.declare_table(&"players", [&"score"])
## [/codeblock]
class Dict:
	extends NetwDatabaseBackend

	# namespace -> table -> id -> Dictionary
	var _data: Dictionary = { }

	# Active save-slot namespace, prefixing every table access.
	var _namespace: String = ""


	func initialize(_schema: Dictionary, slot: String = "") -> Error:
		_namespace = slot
		if not _data.has(_namespace):
			_data[_namespace] = { }
		return OK


	func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
		var ns := _ns()
		if not ns.has(table):
			ns[table] = { }

		if not ns[table].has(id):
			ns[table][id] = { }

		for key in data:
			ns[table][id][key] = data[key]

		return OK


	func find_by_id(table: StringName, id: StringName) -> Dictionary:
		var ns := _ns()
		if ns.has(table) and ns[table].has(id):
			return ns[table][id].duplicate()
		return { }


	func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
		var ns := _ns()
		if not ns.has(table):
			return []

		var results: Array[Dictionary] = []
		for id in ns[table]:
			var record: Dictionary = ns[table][id]
			if _matches_filter(record, filter):
				results.append(record.duplicate())

		return results


	func delete(table: StringName, id: StringName) -> Error:
		var ns := _ns()
		if ns.has(table):
			ns[table].erase(id)
		return OK


	func list_namespaces() -> Array[StringName]:
		var out: Array[StringName] = []
		for key in _data:
			out.append(StringName(key))
		return out


	func delete_namespace(slot: String) -> Error:
		_data.erase(slot)
		return OK


	# Returns the table map for the active namespace, creating it on first use.
	func _ns() -> Dictionary:
		if not _data.has(_namespace):
			_data[_namespace] = { }
		return _data[_namespace]


	func _matches_filter(record: Dictionary, filter: Dictionary) -> bool:
		for key in filter:
			if not record.has(key) or record[key] != filter[key]:
				return false
		return true
