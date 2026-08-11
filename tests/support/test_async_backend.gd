## Deliberately asynchronous [NetwDatabaseBackend] fake.
##
## Every verb awaits a frame before answering, which is what a backend talking
## to a network service does. The twin of [TestMemoryBackend]: same store, same
## merge, and the only difference is the await, so a law that holds for one and
## fails for the other is a law about the asynchronous path rather than about
## storage.
class_name TestAsyncBackend
extends NetwDatabaseBackend

var _store: Dictionary = { }
var _tree: SceneTree


func _init(tree: SceneTree = null) -> void:
	_tree = tree


func _initialize(_schema: Dictionary, _slot: String = "") -> Error:
	await _yield()
	return OK


func _upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
	await _yield()
	if not _store.has(table):
		_store[table] = { }
	var existing: Dictionary = (_store[table].get(id, { }) as Dictionary) \
			.duplicate()
	for key in data:
		existing[key] = data[key]
	_store[table][id] = existing
	return OK


func _find_by_id(table: StringName, id: StringName) -> Dictionary:
	await _yield()
	if not _store.has(table):
		return { }
	return (_store[table].get(id, { }) as Dictionary).duplicate()


func _find_all(table: StringName, _filter: Dictionary = { }) -> Array[Dictionary]:
	await _yield()
	var out: Array[Dictionary] = []
	return out


func _delete(table: StringName, id: StringName) -> Error:
	await _yield()
	if _store.has(table):
		_store[table].erase(id)
	return OK


func _list_namespaces() -> Array[StringName]:
	await _yield()
	var out: Array[StringName] = [&"async_slot"]
	return out


func _yield() -> void:
	if _tree:
		await _tree.process_frame
