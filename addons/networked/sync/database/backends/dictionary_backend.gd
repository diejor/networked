## In-memory [NetwBackend] that stores records in a nested [Dictionary].
##
## Ideal for unit testing [SaveComponent] and other systems that depend on
## [NetwDatabase] without wanting the performance overhead or test-isolation
## flakiness of disk I/O.
##
## [b]Note:[/b] Data is lost when the node or resource holding this backend is freed.
class_name DictionaryBackend
extends NetwBackend

# table -> id -> Dictionary
var _data: Dictionary = {}

func initialize(_schema: Dictionary) -> Error:
	return OK

func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	if not _data.has(table):
		_data[table] = {}
	
	if not _data[table].has(id):
		_data[table][id] = {}
		
	for key in data:
		_data[table][id][key] = data[key]
		
	return OK

func find_by_id(table: StringName, id: StringName) -> Dictionary:
	if _data.has(table) and _data[table].has(id):
		return _data[table][id].duplicate()
	return {}

func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
	if not _data.has(table):
		return []
		
	var results: Array[Dictionary] = []
	for id in _data[table]:
		var record: Dictionary = _data[table][id]
		if _matches_filter(record, filter):
			results.append(record.duplicate())
			
	return results

func delete(table: StringName, id: StringName) -> Error:
	if _data.has(table):
		_data[table].erase(id)
	return OK

func _matches_filter(record: Dictionary, filter: Dictionary) -> bool:
	for key in filter:
		if not record.has(key) or record[key] != filter[key]:
			return false
	return true
