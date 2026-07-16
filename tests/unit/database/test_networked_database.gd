## Unit tests for [NetwDatabase].
##
## Covers schema registration, transaction API, and readers.
class_name TestNetwDatabase
extends NetwTestSuite

class FailingBackend extends TestMemoryBackend:
	func _upsert(
			_table: StringName,
			_id: StringName,
			_data: Dictionary,
	) -> Error:
		return ERR_CANT_CREATE


func _make_db(backend: Variant = null) -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = auto_free(backend if backend else TestMemoryBackend.new())
	return db


func test_schema_registration_flow() -> void:
	var db := _make_db()
	var emitted := [false]
	var captured_columns: Array[StringName] = []
	db.schema_registered.connect(
		func(_t, cols: Array[StringName]):
			emitted[0] = true
			captured_columns.assign(cols)
	)

	db._register_schema(&"rocks", [&"health"])
	db._register_schema(&"rocks", [&"position"])
	await get_tree().process_frame

	var record := await db._find_by_id(&"rocks", &"r1")
	assert_that(record.is_empty()).is_true()
	assert_that(emitted[0]).is_true()
	assert_that(captured_columns.has(&"health")).is_true()
	assert_that(captured_columns.has(&"position")).is_true()


func test_transaction_flow() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	var committed := [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	var err := await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 50 })
			tx.queue_upsert(&"rocks", &"r2", { &"health": 20 })
			tx.queue_upsert(&"rocks", &"r3", { &"health": 30 })
	)

	var backend := db.backend as TestMemoryBackend
	assert_that(err).is_equal(OK)
	assert_that(backend.upsert_calls.size()).is_equal(3)
	assert_that(backend.upsert_calls[0].get("id")).is_equal(&"r1")
	assert_that(committed[0]).is_true()

	db = _make_db(FailingBackend.new())
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	committed = [false]
	db.transaction_committed.connect(func(_tc, _rc): committed[0] = true)
	err = await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 10 })
	)
	assert_that(err).is_equal(ERR_CANT_CREATE)
	assert_that(committed[0]).is_false()


func test_reader_and_delete_flow() -> void:
	var db := _make_db()
	db._register_schema(&"rocks", [&"health"])
	await get_tree().process_frame

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"rocks", &"r1", { &"health": 99 })
			tx.queue_upsert(&"rocks", &"r2", { &"health": 20 })
	)

	var hits: Array[bool] = []
	db.record_loaded.connect(func(_t, _id, hit: bool): hits.append(hit))
	var record := await db._find_by_id(&"rocks", &"r1")
	assert_that(record.get(&"health")).is_equal(99)

	await db._find_by_id(&"rocks", &"r1")
	await db._find_by_id(&"rocks", &"nonexistent")
	assert_that(hits).contains_exactly([true, true, false])

	var all := await db._find_all(&"rocks")
	assert_that(all.size()).is_equal(2)

	await db.delete(&"rocks", &"r1")
	var backend := db.backend as TestMemoryBackend
	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that((await db._find_by_id(&"rocks", &"r1")).is_empty()).is_true()


func test_schema_mismatch_signal_flow() -> void:
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
