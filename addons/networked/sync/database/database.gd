## Schema registry and API surface for the networked persistence layer.
##
## Save this resource as a [code].tres[/code] file and assign it to the
## [member SaveComponent.database] export on any entity that should participate
## in the database.
##
## [br][br]
## [b]Table access:[/b] retrieve a [NetwDatabase.TableRepository] for a named
## table with [method table], then fetch, put, or delete [Entity] records:
## [codeblock]
## # Fetch a player entity:
## var entity := db.table(&"players").fetch(username)
##
## # Property-style access also works (autocomplete via _get_property_list):
## var entity := db.players.fetch(username)
## [/codeblock]
##
## [b]Typed tables:[/b] register an entity subclass so
## [method NetwDatabase.TableRepository.fetch] returns the right type automatically:
## [codeblock]
## db.declare_table(&"rocks", [&"health", &"position"], RockEntity)
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
##
## [b]Transaction API:[/b] batch writes via a closure to guarantee the commit
## always runs:
## [codeblock]
## db.transaction(func(tx: NetwDatabase.TransactionContext):
##     tx.queue_upsert(&"rocks", &"rock_1", {&"health": 50})
##     tx.queue_upsert(&"rocks", &"rock_2", {&"health": 75})
## )
## [/codeblock]
##
## [b]Schema mismatch:[/b] when a loaded record contains columns not present in
## the current schema (e.g. after a property rename) the
## [member mismatch_policy] determines what happens — see
## [enum SchemaMismatchPolicy].
class_name NetwDatabase
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
## [param missing] = columns in schema but not in the record (safe — use scene
## default).
signal schema_mismatch(table: StringName, id: StringName, \
		missing: Array[StringName], unknown: Array[StringName])
## Emitted after a transaction is successfully committed.
signal transaction_committed(table_count: int, record_count: int)


## Controls what happens when a loaded record has columns not in the current
## schema.
enum SchemaMismatchPolicy {
	## Delete the record and start fresh.
	PURGE,
	## Strip unknown columns from the loaded data and proceed with known
	## columns only. Properties missing from the record get their
	## scene-default values.
	LOAD_PARTIAL,
	## Return [constant ERR_UNCONFIGURED] and leave the record untouched.
	## The caller is responsible for deciding what to do.
	FAIL,
}

## The storage backend. Must be set before the first schema registration call
## triggers [method _initialize_backend].
@export var backend: NetwBackend

## What to do when a loaded record has columns absent from the current schema.
@export var mismatch_policy: SchemaMismatchPolicy = SchemaMismatchPolicy.PURGE

# table → Array[StringName] of declared column names
var _schema: Dictionary[StringName, Array] = {}
var _initialized: bool = false

# table → Script (entity subclass)
var _table_scripts: Dictionary[StringName, Script] = {}


# ── Binding ───────────────────────────────────────────────────────────────────

## Binds a [SaveComponent] to this database, registering its schema.
## If [param span] is provided, steps are recorded for the initialization
## process.
func bind(component: SaveComponent, span: NetSpan = null) -> void:
	if component.table_name.is_empty():
		return
	
	var columns := component.get_virtual_properties()
	_register_schema(component.table_name, columns)
	
	if span:
		span.step("schema_registered", {
			table = component.table_name,
			columns = columns
		})


# ── Table access ──────────────────────────────────────────────────────────────

## Returns the [NetwDatabase.TableRepository] for [param table_name].
##
## Repositories are cached — repeated calls for the same name return the same
## instance. The table does not need to be registered before calling this;
## registration is done separately via [method _register_schema] or
## [method declare_table].
## [codeblock lang=gdscript]
## var entity := db.table(&"players").fetch(username)
## [/codeblock]
func table(table_name: StringName) -> TableRepository:
	var script := _table_scripts.get(table_name)
	return TableRepository.new(self, table_name, script)


## Declares [param table_name] with [param columns] and an optional typed
## [param entity_script].
##
## This is the public entry point for build-time or power-user schema
## declaration. Tables declared here are known before any runtime query,
## preventing silent schema-mismatch destruction of data.
## [codeblock lang=gdscript]
## db.declare_table(&"rocks", [&"health", &"position"], RockEntity)
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
func declare_table(
		table_name: StringName,
		columns: Array[StringName] = [],
		entity_script: Script = null) -> void:
	if entity_script:
		_table_scripts[table_name] = entity_script
	
	if not columns.is_empty():
		_register_schema(table_name, columns)


# ── Dynamic property proxy ────────────────────────────────────────────────────

## Exposes registered tables as properties for ergonomic access.
##
## Any registered table name resolves to its [NetwDatabase.TableRepository]:
## [codeblock lang=gdscript]
## var entity := db.players.fetch(username)   # same as db.table(&"players").fetch(...)
## [/codeblock]
func _get(property: StringName) -> Variant:
	if _schema.has(property):
		return table(property)
	return null

## Makes registered table names visible to the GDScript autocomplete and
## property inspector.
func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	for tname: StringName in _schema:
		props.append({
			"name": tname,
			"type": TYPE_OBJECT,
			"hint": PROPERTY_HINT_RESOURCE_TYPE,
			"hint_string": "NetwDatabase.TableRepository",
			"usage": PROPERTY_USAGE_NONE,
		})
	return props


# ── Schema Registration ───────────────────────────────────────────────────────

# Declares the columns for [param table].
#
# Called automatically by [SaveComponent] during sync setup.
# Calling this again for the same table merges any new columns in.
# Triggers backend initialization the first time any table is registered
# (deferred so that multiple [SaveComponent] nodes registering in the same
# frame are batched into one [method _initialize_backend] call).
func _register_schema(table: StringName, columns: Array[StringName]) -> void:
	if not _schema.has(table):
		_schema[table] = [] as Array[StringName]
	
	var existing: Array[StringName] = _schema[table]
	for col in columns:
		if not existing.has(col):
			existing.append(col)
	
	schema_registered.emit(table, existing.duplicate())
	
	if not _initialized:
		_initialize_backend()


## Returns the registered column names for [param table], or an empty array if
## the table has not been registered yet.
func get_registered_columns(table: StringName) -> Array[StringName]:
	return (_schema.get(table, [] as Array[StringName]) \
			as Array[StringName]).duplicate()


# Initializes the backend and registers all known schemas.
func _initialize_backend() -> void:
	if _initialized:
		return
	_initialized = true
	if not backend:
		Netw.dbg.error("NetwDatabase: no backend assigned. " + \
				"Calls will be no-ops.", func(m): push_error(m))
		return
	var err := backend.initialize(_schema)
	if err != OK:
		Netw.dbg.error("NetwDatabase: backend initialization failed. " + \
				"Error: %s", [error_string(err)], func(m): push_error(m))


# ── Schema diffing ────────────────────────────────────────────────────────────

# Compares [param record] against the declared schema for [param table].
#
# Returns a [Dictionary] with:
# - [code]missing[/code] ([Array][StringName]): schema columns absent from the
#   record (safe).
# - [code]unknown[/code] ([Array][StringName]): record columns not in the
#   schema (triggers policy).
# - [code]ok[/code] ([bool]): [code]true[/code] when both arrays are empty.
func _diff_record(table: StringName, id: StringName, record: Dictionary) -> Dictionary:
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


# Applies [member mismatch_policy] to [param record] given a diff result.
#
# Returns the (possibly modified) record dictionary on success, or an empty
# [Dictionary] with an accompanying error code when the policy demands failure
# or deletion.
# [param out_error] receives the error code ([constant OK] on success).
func _apply_mismatch_policy(
		table: StringName,
		id: StringName,
		record: Dictionary,
		diff: Dictionary,
		out_error: Array) -> Dictionary:
	
	out_error[0] = OK
	
	if diff.ok:
		return record
	
	# Columns present in the schema but absent from the record are new
	# additions — the scene will supply defaults. No policy action required.
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
## [param body] receives a [NetwDatabase.TransactionContext] and should call
## [method NetwDatabase.TransactionContext.queue_upsert] for each record to
## write. The transaction is committed after [param body] returns.
## Returns [constant OK] on success or the first error returned by the backend.
func transaction(body: Callable) -> Error:
	if not backend:
		Netw.dbg.error("NetwDatabase: transaction called but no backend " + \
				"is set.", func(m): push_error(m))
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


# ── Internal readers ──────────────────────────────────────────────────────────

# Returns the raw record for [param id] in [param table].
#
# Applies [member mismatch_policy] if the stored record has columns not in the
# schema. [param out_error] is an [Array] with one element; set to
# [constant OK] on success, [constant ERR_FILE_NOT_FOUND] when the record does
# not exist, or [constant ERR_UNCONFIGURED] when the table has no registered
# schema.
func _find_by_id(table: StringName, id: StringName, out_error: Array = [OK]) -> Dictionary:
	if not _schema.has(table):
		Netw.dbg.error(
			"NetwDatabase: read on unregistered table '%s'. ", [table],
			func(m): push_error(m)
		)
		push_warning(
			"NetwDatabase: read on unregistered table '%s'. " % [table]
			+ "Declare the schema before querying."
		)
		out_error[0] = ERR_UNCONFIGURED
		return {}
	
	if not backend:
		Netw.dbg.error("NetwDatabase: _find_by_id called but no backend " + \
				"is set.", func(m): push_error(m))
		out_error[0] = ERR_UNCONFIGURED
		return {}
	
	var record := backend.find_by_id(table, id)
	var hit := not record.is_empty()
	record_loaded.emit(table, id, hit)
	
	if not hit:
		out_error[0] = ERR_FILE_NOT_FOUND
		return {}
	
	var diff := _diff_record(table, id, record)
	if not diff.ok:
		record = _apply_mismatch_policy(table, id, record, diff, out_error)
	
	return record


# Returns all raw records in [param table] matching [param filter].
# An empty filter returns every record.
func _find_all(table: StringName, filter: Dictionary = {}) -> Array[Dictionary]:
	if not _schema.has(table):
		Netw.dbg.error(
			"NetwDatabase: read on unregistered table '%s'. ", [table],
			func(m): push_error(m)
		)
		push_warning(
			"NetwDatabase: read on unregistered table '%s'. " % [table]
			+ "Declare the schema before querying."
		)
		return []
	
	if not backend:
		Netw.dbg.error("NetwDatabase: _find_all called but no backend is set.",
				func(m): push_error(m))
		return []
	return backend.find_all(table, filter)


## Permanently removes [param id] from [param table].
func delete(table: StringName, id: StringName) -> Error:
	return _delete_internal(table, id)


# Deletes a record from the backend.
func _delete_internal(table: StringName, id: StringName) -> Error:
	if not backend:
		Netw.dbg.error("NetwDatabase: delete called but no backend is set.",
				func(m): push_error(m))
		return ERR_UNCONFIGURED
	return backend.delete(table, id)


# ── TransactionContext ────────────────────────────────────────────────────────

## Accumulates write operations inside a [method NetwDatabase.transaction]
## closure.
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
	func _commit(backend: NetwBackend) -> Error:
		for entry in _queue:
			var err := backend.upsert(entry.table, entry.id, entry.data)
			if err != OK:
				return err
		return OK

## A typed read/write interface for a single database table.
##
## Obtain a repository from [NetwDatabase] via [method NetwDatabase.table].
## Repositories surface CRUD operations and return hydrated [Entity] instances,
## keeping all I/O knowledge inside the persistence layer.
##
## [codeblock lang=gdscript]
## # Fetch an entity and bind it to a SaveComponent:
## var entity := db.table(&"players").fetch(username)
## if entity:
##     save_comp.bound_entity = entity
##
## # Write changes back to the database:
## var err := db.table(&"players").put(username, save_comp.bound_entity)
##
## # Delete a record:
## db.table(&"players").delete(username)
##
## # Fetch every entity in the table:
## for entity in db.table(&"players").fetch_all():
##     print(entity.get_value(&"score"))
## [/codeblock]
##
## [b]Custom entity classes:[/b] register an entity script via
## [method NetwDatabase.declare_table] so [method fetch] returns a typed
## subclass rather than a [DictionaryEntity]:
## [codeblock lang=gdscript]
## db.declare_table(&"rocks", [&"health", &"position"], RockEntity)
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
class TableRepository:

	var _db: NetwDatabase
	var _table: StringName
	var _entity_script: Script

	func _init(db: NetwDatabase, table: StringName, entity_script: Script = null) -> void:
		_db = db
		_table = table
		_entity_script = entity_script


	## Returns the column names registered for this table, or an empty array.
	func get_columns() -> Array[StringName]:
		return _db.get_registered_columns(_table)


	## Fetches the record for [param id] and returns it as a hydrated [Entity].
	##
	## Returns [code]null[/code] when the record does not exist, the table has no
	## registered schema, or a schema-mismatch policy blocks the load.
	## The caller assigns the entity to the component:
	## [codeblock lang=gdscript]
	## save_comp.bound_entity = db.table(&"players").fetch(username)
	## [/codeblock]
	func fetch(id: StringName) -> Entity:
		var out_error: Array[int] = [OK]
		var record: Dictionary = _db._find_by_id(_table, id, out_error)
		if out_error[0] != OK or record.is_empty():
			return null
		var entity: Entity = _make_entity()
		entity.from_dict(record)
		return entity


	## Writes [param entity] to the database under [param id].
	##
	## Uses [method Entity.to_dict] to produce the record; returns
	## [constant OK] on success or the first backend error encountered.
	## [codeblock lang=gdscript]
	## var err := db.table(&"players").put(username, save_comp.bound_entity)
	## [/codeblock]
	func put(id: StringName, entity: Entity) -> Error:
		return _db.transaction(func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(_table, id, entity.to_dict())
		)


	## Permanently removes [param id] from the table.
	## Idempotent — returns [constant OK] even when the record does not exist.
	func delete(id: StringName) -> Error:
		return _db.delete(_table, id)


	## Returns every entity in the table matching [param filter].
	## An empty [param filter] returns all records.
	##
	## [codeblock lang=gdscript]
	## var active_players := db.table(&"players").fetch_all({&"online": true})
	## [/codeblock]
	func fetch_all(filter: Dictionary = {}) -> Array[Entity]:
		var results: Array[Entity] = []
		for record in _db._find_all(_table, filter):
			var entity: Entity = _make_entity()
			entity.from_dict(record)
			results.append(entity)
		return results


	func _make_entity() -> Entity:
		if _entity_script:
			return _entity_script.new() as Entity
		return DictionaryEntity.new()
