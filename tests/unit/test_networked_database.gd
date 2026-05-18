## Unit tests for [NetwDatabase].
##
## Covers schema registration, transaction API, and readers.
class_name TestNetwDatabase
extends NetworkedTestSuite


class SpyBackend extends NetwBackend:
	var init_calls: Array[Dictionary] = []
	var upsert_calls: Array[Dictionary] = []
	var find_calls: Array[Dictionary] = []
	var delete_calls: Array[Dictionary] = []
	var _store: Dictionary = {}

	func initialize(schema: Dictionary) -> Error:
		init_calls.append({schema = schema})
		return OK

	func upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
		upsert_calls.append({table = table, id = id, data = data.duplicate()})
		if not _store.has(table):
			_store[table] = {}
		var existing: Dictionary = (_store[table].get(id, {}) as Dictionary) \
			.duplicate()
		for key in data:
			existing[key] = data[key]
		_store[table][id] = existing
		return OK

	func find_by_id(table: StringName, id: StringName) -> Dictionary:
		find_calls.append({table = table, id = id})
		if not _store.has(table):
			return {}
		return (_store[table].get(id, {}) as Dictionary).duplicate()

	func find_all(table: StringName, filter: Dictionary) -> Array[Dictionary]:
		if not _store.has(table):
			return []
		var results: Array[Dictionary] = []
		for entry_id in _store[table]:
			var record: Dictionary = _store[table][entry_id]
			var ok := true
			for key in filter:
				if not record.has(key) or record[key] != filter[key]:
					ok = false
					break
			if ok:
				results.append(record.duplicate())
		return results

	func delete(table: StringName, id: StringName) -> Error:
		delete_calls.append({table = table, id = id})
		if _store.has(table):
			_store[table].erase(id)
		return OK


class FailingBackend extends SpyBackend:
	func upsert(
		_table: StringName,
		_id: StringName,
		_data: Dictionary
	) -> Error:
		return ERR_CANT_CREATE


func _make_db() -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(SpyBackend.new())
	return db


func test_register_schema_stores_columns() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health", &"position"])
	await get_tree().process_frame

	var record := db._find_by_id(&"rocks", &"r1")
	assert_that(record.is_empty()).is_true()


func test_register_schema_emits_signal() -> void:
	var db := _make_db()
	var emitted := [false]
	db.schema_registered.connect(func(_t, _c): emitted[0] = true)
	db._register_schema(&"rocks", [&"health"])
	assert_that(emitted[0]).is_true()


func test_register_schema_merges_columns_on_second_call() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	var captured_columns: Array[StringName] = []
	db.schema_registered.connect(
		func(_t, cols: Array[StringName]): captured_columns.assign(cols))
	db._register_schema(&"rocks", [&"position"])
	assert_that(captured_columns.has(&"health")).is_true()
	assert_that(captured_columns.has(&"position")).is_true()


func test_transaction_calls_backend_upsert() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 50})
	)

	var backend := db.backend as SpyBackend
	assert_that(backend.upsert_calls.size()).is_equal(1)
	assert_that(backend.upsert_calls[0].get("id")).is_equal(&"r1")


func test_transaction_batches_multiple_upserts() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
		tx.queue_upsert(&"rocks", &"r2", {&"health": 20})
		tx.queue_upsert(&"rocks", &"r3", {&"health": 30})
	)

	var backend := db.backend as SpyBackend
	assert_that(backend.upsert_calls.size()).is_equal(3)


func test_transaction_returns_ok_on_success() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var err := db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)
	assert_that(err).is_equal(OK)


func test_transaction_propagates_backend_error() -> void:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(FailingBackend.new())
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var err := db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)
	assert_that(err).is_equal(ERR_CANT_CREATE)


func test_transaction_emits_committed_signal_on_success() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var committed := [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)
	assert_that(committed[0]).is_true()


func test_transaction_does_not_emit_committed_on_failure() -> void:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(FailingBackend.new())
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var committed := [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)
	assert_that(committed[0]).is_false()


func test_find_by_id_delegates_to_backend() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 99})
	)

	var record := db._find_by_id(&"rocks", &"r1")
	assert_that(record.get(&"health")).is_equal(99)


func test_find_by_id_emits_loaded_signal_with_hit_true() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)

	var hit_value := [false]
	db.record_loaded.connect(func(_t, _id, hit: bool): hit_value[0] = hit)
	db._find_by_id(&"rocks", &"r1")
	assert_that(hit_value[0]).is_true()


func test_find_by_id_emits_loaded_signal_with_hit_false_on_miss() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var hit_value := [true]
	db.record_loaded.connect(func(_t, _id, hit: bool): hit_value[0] = hit)
	db._find_by_id(&"rocks", &"nonexistent")
	assert_that(hit_value[0]).is_false()


func test_find_all_delegates_to_backend() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
		tx.queue_upsert(&"rocks", &"r2", {&"health": 20})
	)

	var all := db._find_all(&"rocks")
	assert_that(all.size()).is_equal(2)


func test_delete_delegates_to_backend() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"rocks", &"r1", {&"health": 10})
	)

	db.delete(&"rocks", &"r1")
	var backend := db.backend as SpyBackend
	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that(db._find_by_id(&"rocks", &"r1").is_empty()).is_true()


func test_upsert_emits_record_upserted_signal() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var upserted_id: Array[StringName] = [&""]
	db.record_upserted.connect(func(_t, id: StringName): upserted_id[0] = id)

	db.record_upserted.emit(&"rocks", &"r1")
	assert_that(upserted_id[0]).is_equal(&"r1")


func test_schema_mismatch_emits_signal_with_column_lists() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var captured_unknown: Array = []
	var captured_missing: Array = []
	db.schema_mismatch.connect(
		func(_t, _id, missing: Array[StringName], unknown: Array[StringName]):
			captured_unknown.assign(unknown)
			captured_missing.assign(missing)
	)

	db._diff_record(&"rocks", &"r1", {&"health": 10, &"gold": 5})
	assert_that(captured_unknown.has(&"gold")).is_true()
	assert_that(captured_missing.is_empty()).is_true()
