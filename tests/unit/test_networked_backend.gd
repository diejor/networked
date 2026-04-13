## Verifies the [NetworkedBackend] abstract contract via a minimal stub.
##
## The abstract class itself cannot be instantiated (Godot prevents it),
## so we test through a concrete stub that delegates to an in-memory dictionary.
class_name TestNetworkedBackend
extends NetworkedTestSuite


## In-memory stub implementing the full [NetworkedBackend] contract.
class MemoryBackend extends NetworkedBackend:
	## { table → { id → data } }
	var _store: Dictionary = {}

	func _initialize(_schema: Dictionary) -> Error:
		return OK

	func _upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
		if not _store.has(table):
			_store[table] = {}
		var existing: Dictionary = _store[table].get(id, {}).duplicate()
		for key in data:
			existing[key] = data[key]
		_store[table][id] = existing
		return OK

	func _find_by_id(table: StringName, id: StringName) -> Dictionary:
		if not _store.has(table):
			return {}
		return (_store[table].get(id, {}) as Dictionary).duplicate()

	func _find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
		if not _store.has(table):
			return []
		var results: Array[Dictionary] = []
		for id in _store[table]:
			var record: Dictionary = _store[table][id]
			var match_filter := true
			for key in filter:
				if not record.has(key) or record[key] != filter[key]:
					match_filter = false
					break
			if match_filter:
				results.append(record.duplicate())
		return results

	func _delete(table: StringName, id: StringName) -> Error:
		if _store.has(table):
			_store[table].erase(id)
		return OK


# ---------------------------------------------------------------------------
# Stub contract tests
# ---------------------------------------------------------------------------

func test_initialize_returns_ok() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	var err: Error = backend._initialize({})
	assert_that(err).is_equal(OK)


func test_upsert_and_find_by_id_round_trip() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	var err: Error = backend._upsert(&"rocks", &"rock_1", {&"health": 50})
	assert_that(err).is_equal(OK)

	var record: Dictionary = backend._find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(50)


func test_upsert_merges_columns() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	backend._upsert(&"rocks", &"rock_1", {&"health": 50})
	backend._upsert(&"rocks", &"rock_1", {&"gold": 10})

	var record: Dictionary = backend._find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(50)
	assert_that(record.get(&"gold")).is_equal(10)


func test_find_by_id_returns_empty_for_missing_record() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	var record: Dictionary = backend._find_by_id(&"rocks", &"nonexistent")
	assert_that(record.is_empty()).is_true()


func test_find_all_returns_all_records() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	backend._upsert(&"rocks", &"r1", {&"type": &"granite"})
	backend._upsert(&"rocks", &"r2", {&"type": &"marble"})
	backend._upsert(&"rocks", &"r3", {&"type": &"granite"})

	var all: Array[Dictionary] = backend._find_all(&"rocks", {})
	assert_that(all.size()).is_equal(3)


func test_find_all_with_filter() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	backend._upsert(&"rocks", &"r1", {&"type": &"granite"})
	backend._upsert(&"rocks", &"r2", {&"type": &"marble"})
	backend._upsert(&"rocks", &"r3", {&"type": &"granite"})

	var granite: Array[Dictionary] = backend._find_all(&"rocks", {&"type": &"granite"})
	assert_that(granite.size()).is_equal(2)


func test_delete_removes_record() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	backend._upsert(&"rocks", &"rock_1", {&"health": 50})
	backend._delete(&"rocks", &"rock_1")

	var record: Dictionary = backend._find_by_id(&"rocks", &"rock_1")
	assert_that(record.is_empty()).is_true()


func test_delete_is_idempotent() -> void:
	var backend: MemoryBackend = auto_free(MemoryBackend.new())
	var err: Error = backend._delete(&"rocks", &"nonexistent")
	assert_that(err).is_equal(OK)
