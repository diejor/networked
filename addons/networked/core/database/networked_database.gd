## Schema registry and API surface for the networked persistence layer.
##
## Save this resource as a [code].tres[/code] file and assign it to the
## [member SaveComponent.database] export on any entity that should participate
## in the database. At runtime, each [SaveComponent] calls [method register_schema]
## to declare its table and columns; the database initializes its backend once all
## schemas are known.
##
## [b]Table access:[/b] retrieve a [TableRepository] for a named table with
## [method table], then fetch, save, or delete [Entity] records:
## [codeblock lang=gdscript]
## # Bind a player entity before spawn:
## save_comp.bound_entity = db.table(&"players").fetch(username)
##
## # Property-style access also works (autocomplete via _get_property_list):
## save_comp.bound_entity = db.players.fetch(username)
## [/codeblock]
##
## [b]Typed tables:[/b] register an entity subclass so [method TableRepository.fetch]
## returns the right type automatically:
## [codeblock lang=gdscript]
## db.register_table(&"rocks", RockEntity)
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
##
## [b]Transaction API:[/b] batch writes via a closure to guarantee the commit always runs:
## [codeblock lang=gdscript]
## db.transaction(func(tx: NetworkedDatabase.TransactionContext):
##     tx.queue_upsert(&"rocks", &"rock_1", {&"health": 50})
##     tx.queue_upsert(&"rocks", &"rock_2", {&"health": 75})
## )
## [/codeblock]
##
## [b]Schema mismatch:[/b] when a loaded record contains columns not present in the
## current schema (e.g. after a property rename) the [member mismatch_policy]
## determines what happens — see [enum SchemaMismatchPolicy].
class_name NetworkedDatabase
extends Resource

## Emitted after a record is written (upserted) to the backend.
signal record_upserted(table: StringName, id: StringName)
## Emitted after a record is read from the backend.
## [param hit] is [code]false[/code] when no record was found.
signal record_loaded(table: StringName, id: StringName, hit: bool)
## Emitted when a table schema is registered for the first time.
signal schema_registered(table: StringName, columns: Array[StringName])
## Emitted when a loaded record contains columns missing from the current schema
## or is missing columns the schema declares.
## [param unknown] = columns in the record but not in schema (triggers policy).
## [param missing] = columns in schema but not in the record (safe — use scene default).
signal schema_mismatch(table: StringName, id: StringName, missing: Array[StringName], unknown: Array[StringName])
## Emitted after a transaction is successfully committed.
signal transaction_committed(table_count: int, record_count: int)


## Controls what happens when a loaded record has columns not in the current schema.
enum SchemaMismatchPolicy {
	## Delete the record and start fresh. Mirrors the existing disk behaviour in
	## [SaveComponent.push_to_scene] which removes the file on [constant ERR_UNCONFIGURED].
	PURGE,
	## Strip unknown columns from the loaded data and proceed with known columns only.
	## Properties missing from the record get their scene-default values.
	LOAD_PARTIAL,
	## Return [constant ERR_UNCONFIGURED] and leave the record untouched.
	## The caller is responsible for deciding what to do.
	FAIL,
}

## The storage backend. Must be set before the first [method register_schema] call
## triggers [method _initialize_backend].
@export var backend: NetworkedBackend

## What to do when a loaded record has columns absent from the current schema.
@export var mismatch_policy: SchemaMismatchPolicy = SchemaMismatchPolicy.PURGE

# table → Array[StringName] of declared column names
var _schema: Dictionary[StringName, Array] = {}
var _initialized: bool = false

# Cached TableRepository instances, keyed by table name.
var _repositories: Dictionary[StringName, TableRepository] = {}


# ── Table access ──────────────────────────────────────────────────────────────

## Returns the [TableRepository] for [param table_name].
##
## Repositories are cached — repeated calls for the same name return the same instance.
## The table does not need to be registered before calling this; registration is done
## separately via [method register_schema] or [method register_table].
## [codeblock lang=gdscript]
## var entity := db.table(&"players").fetch(username)
## [/codeblock]
func table(table_name: StringName) -> TableRepository:
	if not _repositories.has(table_name):
		_repositories[table_name] = TableRepository.new(self, table_name)
	return _repositories[table_name]


## Registers [param table_name] with an optional typed [param entity_script] and
## an optional explicit [param columns] list.
##
## When [param entity_script] is provided, [method TableRepository.fetch] and
## [method TableRepository.fetch_all] will return instances of that class instead
## of [DictionaryEntity]. When [param columns] is omitted and the entity script
## overrides [method Entity.to_dict], columns are inferred later from the first
## [method SaveComponent.instantiate] call.
## [codeblock lang=gdscript]
## db.register_table(&"rocks", RockEntity, [&"health", &"position"])
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
func register_table(
		table_name: StringName,
		entity_script: Script = null,
		columns: Array[StringName] = []) -> void:
	if not _repositories.has(table_name):
		_repositories[table_name] = TableRepository.new(self, table_name, entity_script)
	else:
		_repositories[table_name]._entity_script = entity_script

	if not columns.is_empty():
		register_schema(table_name, columns)


# ── Dynamic property proxy ────────────────────────────────────────────────────

## Exposes registered tables as properties for ergonomic access.
##
## Any registered table name resolves to its [TableRepository]:
## [codeblock lang=gdscript]
## var entity := db.players.fetch(username)   # same as db.table(&"players").fetch(...)
## [/codeblock]
func _get(property: StringName) -> Variant:
	if _schema.has(property):
		return table(property)
	return null

## Makes registered table names visible to the GDScript autocomplete and property inspector.
func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	for tname: StringName in _schema:
		props.append({
			"name": tname,
			"type": TYPE_OBJECT,
			"hint": PROPERTY_HINT_RESOURCE_TYPE,
			"hint_string": "TableRepository",
			"usage": PROPERTY_USAGE_NONE,
		})
	return props


# ── Schema Registration ───────────────────────────────────────────────────────

## Declares the columns for [param table].
##
## Called automatically by [SaveComponent] during [method SaveComponent.instantiate].
## Calling this again for the same table merges any new columns in.
## Triggers backend initialization the first time any table is registered
## (deferred so that multiple [SaveComponent] nodes registering in the same frame
## are batched into one [method _initialize_backend] call).
func register_schema(table: StringName, columns: Array[StringName]) -> void:
	if not _schema.has(table):
		_schema[table] = [] as Array[StringName]

	var existing: Array[StringName] = _schema[table]
	for col in columns:
		if not existing.has(col):
			existing.append(col)

	schema_registered.emit(table, existing.duplicate())

	if not _initialized:
		_initialize_backend.call_deferred()


## Returns the registered column names for [param table], or an empty array if
## the table has not been registered yet.
func get_registered_columns(table: StringName) -> Array[StringName]:
	return (_schema.get(table, [] as Array[StringName]) as Array[StringName]).duplicate()


func _initialize_backend() -> void:
	if _initialized:
		return
	_initialized = true
	if not backend:
		NetLog.error("NetworkedDatabase: no backend assigned. Calls will be no-ops.", [], func(m): push_error(m))
		return
	var err := backend._initialize(_schema)
	if err != OK:
		NetLog.error("NetworkedDatabase: backend initialization failed. Error: %s", [error_string(err)], func(m): push_error(m))


# ── Schema diffing ────────────────────────────────────────────────────────────

## Compares [param record] against the declared schema for [param table].
##
## Returns a [Dictionary] with:
## - [code]missing[/code] ([Array][StringName]): schema columns absent from the record (safe).
## - [code]unknown[/code] ([Array][StringName]): record columns not in the schema (triggers policy).
## - [code]ok[/code] ([bool]): [code]true[/code] when both arrays are empty.
func diff_record(table: StringName, id: StringName, record: Dictionary) -> Dictionary:
	var schema_cols: Array[StringName] = _schema.get(table, [] as Array[StringName])
	var missing: Array[StringName] = []
	var unknown: Array[StringName] = []

	for col in schema_cols:
		if not record.has(col):
			missing.append(col)

	for key: StringName in record:
		if not schema_cols.has(key):
			unknown.append(key)

	var ok := missing.is_empty() and unknown.is_empty()
	if not ok:
		schema_mismatch.emit(table, id, missing, unknown)

	return {missing = missing, unknown = unknown, ok = ok}


## Applies [member mismatch_policy] to [param record] given a diff result.
##
## Returns the (possibly modified) record dictionary on success, or an empty
## [Dictionary] with an accompanying [constant ERR_UNCONFIGURED] error code when
## the policy demands failure or deletion.
## [param out_error] receives the error code ([constant OK] on success).
func apply_mismatch_policy(
		table: StringName,
		id: StringName,
		record: Dictionary,
		diff: Dictionary,
		out_error: Array) -> Dictionary:

	out_error[0] = OK

	if diff.ok:
		return record

	# Columns present in the schema but absent from the record are new additions —
	# the scene will supply defaults. No policy action required.
	if (diff.unknown as Array[StringName]).is_empty():
		return record

	match mismatch_policy:
		SchemaMismatchPolicy.PURGE:
			_delete_internal(table, id)
			# ERR_FILE_NOT_FOUND signals "clean slate" — callers may fall back to
			# spawner state just like a first-play scenario.
			out_error[0] = ERR_FILE_NOT_FOUND
			return {}

		SchemaMismatchPolicy.LOAD_PARTIAL:
			var schema_cols: Array[StringName] = _schema.get(table, [] as Array[StringName])
			var filtered: Dictionary = {}
			for col in schema_cols:
				if record.has(col):
					filtered[col] = record[col]
			return filtered

		SchemaMismatchPolicy.FAIL:
			out_error[0] = ERR_UNCONFIGURED
			return {}

	return record


# ── Transaction API ───────────────────────────────────────────────────────────

## Collects upserts inside a [Callable] and commits them all at once.
##
## [param body] receives a [TransactionContext] and should call
## [method TransactionContext.queue_upsert] for each record to write.
## The transaction is committed after [param body] returns.
## Returns [constant OK] on success or the first error returned by the backend.
func transaction(body: Callable) -> Error:
	if not backend:
		NetLog.error("NetworkedDatabase: transaction called but no backend is set.", [], func(m): push_error(m))
		return ERR_UNCONFIGURED

	var ctx := TransactionContext.new()
	body.call(ctx)
	var err := ctx._commit(backend)

	if err == OK:
		var tables: Dictionary = {}
		for entry in ctx._queue:
			tables[entry.table] = true
		transaction_committed.emit(tables.size(), ctx._queue.size())

	return err


# ── Agnostic Readers ──────────────────────────────────────────────────────────

## Returns the record for [param id] in [param table].
##
## Applies [member mismatch_policy] if the stored record has columns not in the schema.
## [param out_error] is an [Array] with one element; set to [constant OK] on success
## or [constant ERR_UNCONFIGURED] when the mismatch policy halts the load.
## Pass [code][OK][/code] as the initial value.
func find_by_id(table: StringName, id: StringName, out_error: Array = [OK]) -> Dictionary:
	if not backend:
		NetLog.error("NetworkedDatabase: find_by_id called but no backend is set.", [], func(m): push_error(m))
		out_error[0] = ERR_UNCONFIGURED
		return {}

	var record := backend._find_by_id(table, id)
	var hit := not record.is_empty()
	record_loaded.emit(table, id, hit)

	if not hit:
		return {}

	var diff := diff_record(table, id, record)
	if not diff.ok:
		record = apply_mismatch_policy(table, id, record, diff, out_error)

	return record


## Returns all records in [param table] matching [param filter].
## An empty filter returns every record.
func find_all(table: StringName, filter: Dictionary = {}) -> Array[Dictionary]:
	if not backend:
		NetLog.error("NetworkedDatabase: find_all called but no backend is set.", [], func(m): push_error(m))
		return []
	return backend._find_all(table, filter)


## Permanently removes [param id] from [param table].
func delete(table: StringName, id: StringName) -> Error:
	return _delete_internal(table, id)


func _delete_internal(table: StringName, id: StringName) -> Error:
	if not backend:
		NetLog.error("NetworkedDatabase: delete called but no backend is set.", [], func(m): push_error(m))
		return ERR_UNCONFIGURED
	return backend._delete(table, id)


# ── TransactionContext ────────────────────────────────────────────────────────

## Accumulates write operations inside a [method NetworkedDatabase.transaction] closure.
##
## Call [method queue_upsert] for every record you want to write.
## Commit is triggered automatically when the closure returns.
class TransactionContext:
	## Pending write operations: Array of {table, id, data}.
	var _queue: Array[Dictionary] = []

	## Enqueues an upsert for [param id] in [param table] with [param data].
	func queue_upsert(table: StringName, id: StringName, data: Dictionary) -> void:
		_queue.append({table = table, id = id, data = data})

	## Flushes all queued operations to [param backend].
	## Returns the first error encountered, or [constant OK].
	func _commit(backend: NetworkedBackend) -> Error:
		for entry in _queue:
			var err := backend._upsert(entry.table, entry.id, entry.data)
			if err != OK:
				return err
		return OK
