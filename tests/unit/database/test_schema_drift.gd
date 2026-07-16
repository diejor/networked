## Tests for [NetwDatabase] schema-drift detection and mismatch policies.
##
## Uses the in-memory [TestMemoryBackend] stub so no disk I/O or scene tree is
## required.
class_name TestSchemaDrift
extends NetwTestSuite

func _make_db(policy: NetwDatabase.SchemaMismatchPolicy) -> NetwDatabase:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.mismatch_policy = policy
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	db.backend = backend
	db._register_schema(&"rocks", [&"health", &"position"])
	return db


func test_diff_record_classifies_schema_state() -> void:
	var db := _make_db(NetwDatabase.SchemaMismatchPolicy.FAIL)
	var signal_fired := [false]
	db.schema_mismatch.connect(func(_t, _id, _m, _u): signal_fired[0] = true)

	var diff := db._diff_record(
		&"rocks",
		&"ok",
		{ &"health": 10, &"position": Vector2.ZERO },
	)
	assert_that(diff.ok).is_true()
	assert_that((diff.missing as Array).is_empty()).is_true()
	assert_that((diff.unknown as Array).is_empty()).is_true()

	diff = db._diff_record(&"rocks", &"missing", { &"health": 10 })
	assert_that(diff.ok).is_false()
	assert_that((diff.missing as Array).has(&"position")).is_true()
	assert_that((diff.unknown as Array).is_empty()).is_true()

	diff = db._diff_record(
		&"rocks",
		&"unknown",
		{ &"health": 10, &"position": Vector2.ZERO, &"gold": 5 },
	)
	assert_that(diff.ok).is_false()
	assert_that((diff.unknown as Array).has(&"gold")).is_true()
	assert_that((diff.missing as Array).is_empty()).is_true()
	assert_that(signal_fired[0]).is_true()


func test_purge_policy_flow() -> void:
	var db := _make_db(NetwDatabase.SchemaMismatchPolicy.PURGE)
	var backend := db.backend as TestMemoryBackend
	backend._upsert(&"rocks", &"r1", { &"gold": 5 })

	var record := { &"gold": 5 }
	var diff := db._diff_record(&"rocks", &"r1", record)
	var out := [OK]
	var result := db._apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(out[0]).is_equal(ERR_FILE_NOT_FOUND)
	assert_that(result.is_empty()).is_true()
	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that(backend.delete_calls[0].get("id")).is_equal(&"r1")

	record = { &"health": 10 }
	diff = db._diff_record(&"rocks", &"r2", record)
	out = [OK]
	db._apply_mismatch_policy(&"rocks", &"r2", record, diff, out)

	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that(out[0]).is_equal(OK)


func test_load_partial_policy_flow() -> void:
	var db := _make_db(NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL)
	var backend := db.backend as TestMemoryBackend
	backend._upsert(&"rocks", &"r1", { &"health": 50, &"gold": 5 })

	var record := { &"health": 50, &"position": Vector2.ZERO, &"gold": 5 }
	var diff := db._diff_record(&"rocks", &"r1", record)
	var out := [OK]
	var result := db._apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(out[0]).is_equal(OK)
	assert_that(result.has(&"health")).is_true()
	assert_that(result.has(&"position")).is_true()
	assert_that(result.has(&"gold")).is_false()
	assert_that(backend.delete_calls.is_empty()).is_true()


func test_fail_policy_flow() -> void:
	var db := _make_db(NetwDatabase.SchemaMismatchPolicy.FAIL)
	var backend := db.backend as TestMemoryBackend

	var record := { &"gold": 5 }
	var diff := db._diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db._apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(out[0]).is_equal(ERR_UNCONFIGURED)
	assert_that(backend.delete_calls.is_empty()).is_true()
