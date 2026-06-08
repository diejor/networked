## In-memory [NetwBackend] fake for backend contract tests.
##
## It stores records by table and id, merges upserts like the persistence
## backends, and records calls for tests that need spy assertions.
class_name TestMemoryBackend
extends NetwBackend

var init_calls: Array[Dictionary] = []
var upsert_calls: Array[Dictionary] = []
var find_calls: Array[Dictionary] = []
var delete_calls: Array[Dictionary] = []
var _store: Dictionary = { }


func initialize(schema: Dictionary) -> Error:
	init_calls.append({ schema = schema })
	return OK


func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	upsert_calls.append({ table = table, id = id, data = data.duplicate() })
	if not _store.has(table):
		_store[table] = { }
	var existing: Dictionary = (_store[table].get(id, { }) as Dictionary) \
			.duplicate()
	for key in data:
		existing[key] = data[key]
	_store[table][id] = existing
	return OK


func find_by_id(table: StringName, id: StringName) -> Dictionary:
	find_calls.append({ table = table, id = id })
	if not _store.has(table):
		return { }
	return (_store[table].get(id, { }) as Dictionary).duplicate()


func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
	if not _store.has(table):
		return []
	var results: Array[Dictionary] = []
	for entry_id in _store[table]:
		var record: Dictionary = _store[table][entry_id]
		var match_filter := true
		for key in filter:
			if not record.has(key) or record[key] != filter[key]:
				match_filter = false
				break
		if match_filter:
			results.append(record.duplicate())
	return results


func delete(table: StringName, id: StringName) -> Error:
	delete_calls.append({ table = table, id = id })
	if _store.has(table):
		_store[table].erase(id)
	return OK
