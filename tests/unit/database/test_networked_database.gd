## Unit tests for [NetwDatabase].
##
## Covers schema registration, transaction API, and readers.
class_name TestNetwDatabase
extends NetwTestSuite

class FailingBackend extends TestMemoryBackend:
	func upsert(
			_table: StringName,
			_id: StringName,
			_data: Dictionary,
	) -> Error:
		return ERR_CANT_CREATE


func _make_db() -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(TestMemoryBackend.new())
	return db


func test_register_schema_stores_columns() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health", &"position"])
	await get_tree().process_frame

	var record := await db._find_by_id(&"rocks", &"r1")
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
		func(_t, cols: Array[StringName]): captured_columns.assign(cols)
	)
	db._register_schema(&"rocks", [&"position"])
	assert_that(captured_columns.has(&"health")).is_true()
	assert_that(captured_columns.has(&"position")).is_true()


func test_transaction_upserts_batches_and_commits() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var committed := [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 50 })
			tx.queue_upsert(&"rocks", &"r2", { &"health": 20 })
			tx.queue_upsert(&"rocks", &"r3", { &"health": 30 })
	)

	var backend := db.backend as TestMemoryBackend
	assert_that(backend.upsert_calls.size()).is_equal(3)
	assert_that(backend.upsert_calls[0].get("id")).is_equal(&"r1")
	assert_that(committed[0]).is_true()


func test_transaction_returns_ok_on_success() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var err := await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
	)
	assert_that(err).is_equal(OK)


func test_transaction_propagates_backend_error() -> void:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(FailingBackend.new())
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var err := await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
	)
	assert_that(err).is_equal(ERR_CANT_CREATE)


func test_transaction_does_not_emit_committed_on_failure() -> void:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(FailingBackend.new())
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var committed := [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
	)
	assert_that(committed[0]).is_false()


func test_find_by_id_returns_record_and_loaded_signals() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 99 })
	)

	var hits: Array[bool] = []
	db.record_loaded.connect(func(_t, _id, hit: bool): hits.append(hit))
	var record := await db._find_by_id(&"rocks", &"r1")
	assert_that(record.get(&"health")).is_equal(99)

	await db._find_by_id(&"rocks", &"r1")
	await db._find_by_id(&"rocks", &"nonexistent")
	assert_that(hits).contains_exactly([true, true, false])


func test_find_all_delegates_to_backend() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
			tx.queue_upsert(&"rocks", &"r2", { &"health": 20 })
	)

	var all := await db._find_all(&"rocks")
	assert_that(all.size()).is_equal(2)


func test_delete_delegates_to_backend() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
	)

	await db.delete(&"rocks", &"r1")
	var backend := db.backend as TestMemoryBackend
	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that((await db._find_by_id(&"rocks", &"r1")).is_empty()).is_true()


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

	db._diff_record(&"rocks", &"r1", { &"health": 10, &"gold": 5 })
	assert_that(captured_unknown.has(&"gold")).is_true()
	assert_that(captured_missing.is_empty()).is_true()
