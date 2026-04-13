## Abstract storage contract for [NetworkedDatabase] backends.
##
## Extend this resource to implement a concrete storage strategy (e.g. filesystem,
## SQLite, Postgres). Override all methods prefixed with [code]_[/code].
## The public wrappers on [NetworkedDatabase] call these after schema validation.
##
## All table names and record IDs are [StringName] for cheap equality checks.
## Data payloads are plain [Dictionary] keyed by column [StringName].
@abstract
class_name NetworkedBackend
extends Resource


## Called once after all schemas have been registered via [NetworkedDatabase.register_schema].
## Use this to create directories, open connections, or validate existing data on disk.
## [param schema] maps each table name to its declared column names.
@abstract func _initialize(schema: Dictionary) -> Error


## Writes or updates a single record in [param table] identified by [param id].
## Only the keys present in [param data] are written; existing keys not present
## in [param data] are preserved (merge, not replace).
@abstract func _upsert(table: StringName, id: StringName, data: Dictionary) -> Error


## Returns the record for [param id] in [param table], or an empty [Dictionary]
## if no record exists.
@abstract func _find_by_id(table: StringName, id: StringName) -> Dictionary


## Returns all records in [param table] that match every key/value pair in [param filter].
## An empty [param filter] returns all records in the table.
@abstract func _find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]


## Permanently removes the record for [param id] from [param table].
## Returns [constant OK] even if the record did not exist (idempotent).
@abstract func _delete(table: StringName, id: StringName) -> Error
