## A typed read/write interface for a single database table.
##
## Obtain a repository from [NetworkedDatabase] via [method NetworkedDatabase.table].
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
## var err := db.table(&"players").save(username, save_comp.bound_entity)
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
## [method NetworkedDatabase.register_table] so [method fetch] returns a typed
## subclass rather than a [DictionaryEntity]:
## [codeblock lang=gdscript]
## db.register_table(&"rocks", RockEntity)
## var rock: RockEntity = db.table(&"rocks").fetch(&"rock_1")
## [/codeblock]
class_name TableRepository
extends RefCounted

var _db: NetworkedDatabase
var _table: StringName
var _entity_script: Script  # null → DictionaryEntity

func _init(db: NetworkedDatabase, table: StringName, entity_script: Script = null) -> void:
	_db = db
	_table = table
	_entity_script = entity_script


## Returns the column names registered for this table, or an empty array.
func get_columns() -> Array[StringName]:
	return _db.get_registered_columns(_table)


## Fetches the record for [param id] and returns it as a hydrated [Entity].
##
## Returns [code]null[/code] when the record does not exist or a schema-mismatch
## policy (e.g. [constant NetworkedDatabase.SchemaMismatchPolicy.FAIL]) blocks the load.
## The caller assigns the entity to the component:
## [codeblock lang=gdscript]
## save_comp.bound_entity = db.table(&"players").fetch(username)
## [/codeblock]
func fetch(id: StringName) -> Entity:
	var out_error: Array[int] = [OK]
	var record: Dictionary = _db.find_by_id(_table, id, out_error)
	if out_error[0] != OK or record.is_empty():
		return null
	var entity: Entity = _make_entity()
	entity.from_dict(record)
	return entity


## Writes [param entity] to the database under [param id].
##
## Uses [method Entity.to_dict] to produce the record; returns [constant OK] on
## success or the first backend error encountered.
## [codeblock lang=gdscript]
## var err := db.table(&"players").save(username, save_comp.bound_entity)
## [/codeblock]
func save(id: StringName, entity: Entity) -> Error:
	return _db.transaction(func(tx: NetworkedDatabase.TransactionContext) -> void:
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
	for record in _db.find_all(_table, filter):
		var entity: Entity = _make_entity()
		entity.from_dict(record)
		results.append(entity)
	return results


func _make_entity() -> Entity:
	if _entity_script:
		return _entity_script.new() as Entity
	return DictionaryEntity.new()
