## Tests for [NetworkedDatabase] schema-drift detection and mismatch policies.
##
## Uses the in-memory [MemoryBackend] stub (defined in [TestNetworkedBackend])
## so no disk I/O or scene tree is required.
class_name TestSchemaDrift
extends NetworkedTestSuite


## Minimal in-memory backend for schema drift tests.
class DriftMemoryBackend extends NetworkedBackend:
	var _store: Dictionary = {}
	var delete_calls: Array[Dictionary] = []

	func _initialize(_schema: Dictionary) -> Error:
		return OK

	func _upsert(table: StringName, id: StringName, data: Dictionary) -> Error:
		if not _store.has(table):
			_store[table] = {}
		_store[table][id] = data.duplicate()
		return OK

	func _find_by_id(table: StringName, id: StringName) -> Dictionary:
		if not _store.has(table):
			return {}
		return (_store[table].get(id, {}) as Dictionary).duplicate()

	func _find_all(_table: StringName, _filter: Dictionary) -> Array[Dictionary]:
		return []

	func _delete(table: StringName, id: StringName) -> Error:
		delete_calls.append({table = table, id = id})
		if _store.has(table):
			_store[table].erase(id)
		return OK


func _make_db(policy: NetworkedDatabase.SchemaMismatchPolicy) -> NetworkedDatabase:
	var db: NetworkedDatabase = auto_free(NetworkedDatabase.new())
	db.mismatch_policy = policy
	var backend: DriftMemoryBackend = auto_free(DriftMemoryBackend.new())
	db.backend = backend
	db.register_schema(&"rocks", [&"health", &"position"])
	return db


# ---------------------------------------------------------------------------
# diff_record
# ---------------------------------------------------------------------------

func test_diff_record_ok_when_record_matches_schema() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	var record := {&"health": 10, &"position": Vector2.ZERO}
	var diff := db.diff_record(&"rocks", &"r1", record)
	assert_that(diff.ok).is_true()
	assert_that((diff.missing as Array).is_empty()).is_true()
	assert_that((diff.unknown as Array).is_empty()).is_true()


func test_diff_record_detects_missing_columns() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	# Record only has 'health'; schema also requires 'position'.
	var record := {&"health": 10}
	var diff := db.diff_record(&"rocks", &"r1", record)
	assert_that(diff.ok).is_false()
	assert_that((diff.missing as Array).has(&"position")).is_true()
	assert_that((diff.unknown as Array).is_empty()).is_true()


func test_diff_record_detects_unknown_columns() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	# Record has 'gold' which is not in the schema.
	var record := {&"health": 10, &"position": Vector2.ZERO, &"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	assert_that(diff.ok).is_false()
	assert_that((diff.unknown as Array).has(&"gold")).is_true()
	assert_that((diff.missing as Array).is_empty()).is_true()


func test_diff_record_emits_schema_mismatch_signal() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.FAIL)
	var signal_fired := false
	db.schema_mismatch.connect(func(_t, _id, _m, _u): signal_fired = true)

	db.diff_record(&"rocks", &"r1", {&"gold": 5})
	assert_that(signal_fired).is_true()


# ---------------------------------------------------------------------------
# apply_mismatch_policy — PURGE
# ---------------------------------------------------------------------------

func test_purge_policy_deletes_db_record() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	var backend := db.backend as DriftMemoryBackend
	backend._upsert(&"rocks", &"r1", {&"gold": 5})

	var record := {&"gold": 5}  # 'gold' is unknown in schema
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	# PURGE signals "clean slate" with ERR_FILE_NOT_FOUND so callers may fall back
	# to spawner state just like a first-play scenario.
	assert_that(out[0]).is_equal(ERR_FILE_NOT_FOUND)
	assert_that(backend.delete_calls.size()).is_equal(1)
	assert_that(backend.delete_calls[0].get("id")).is_equal(&"r1")


func test_purge_policy_returns_empty_dict() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	var record := {&"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	var result := db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)
	assert_that(result.is_empty()).is_true()


# ---------------------------------------------------------------------------
# apply_mismatch_policy — LOAD_PARTIAL
# ---------------------------------------------------------------------------

func test_load_partial_strips_unknown_columns() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.LOAD_PARTIAL)
	var record := {&"health": 50, &"position": Vector2.ZERO, &"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	var result := db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(out[0]).is_equal(OK)
	assert_that(result.has(&"health")).is_true()
	assert_that(result.has(&"position")).is_true()
	assert_that(result.has(&"gold")).is_false()


func test_load_partial_does_not_delete_record() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.LOAD_PARTIAL)
	var backend := db.backend as DriftMemoryBackend
	backend._upsert(&"rocks", &"r1", {&"health": 50, &"gold": 5})

	var record := {&"health": 50, &"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(backend.delete_calls.is_empty()).is_true()


# ---------------------------------------------------------------------------
# apply_mismatch_policy — FAIL
# ---------------------------------------------------------------------------

func test_fail_policy_returns_err_unconfigured() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.FAIL)
	var record := {&"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)
	assert_that(out[0]).is_equal(ERR_UNCONFIGURED)


func test_fail_policy_does_not_delete_record() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.FAIL)
	var backend := db.backend as DriftMemoryBackend

	var record := {&"gold": 5}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	assert_that(backend.delete_calls.is_empty()).is_true()


# ---------------------------------------------------------------------------
# diff_record — missing columns only (safe path)
# ---------------------------------------------------------------------------

func test_missing_only_does_not_trigger_delete_in_purge_policy() -> void:
	var db := _make_db(NetworkedDatabase.SchemaMismatchPolicy.PURGE)
	var backend := db.backend as DriftMemoryBackend
	# Record is missing 'position' but has no unknown columns.
	var record := {&"health": 10}
	var diff := db.diff_record(&"rocks", &"r1", record)
	var out := [OK]
	db.apply_mismatch_policy(&"rocks", &"r1", record, diff, out)

	# Missing columns are safe — no delete should be triggered.
	assert_that(backend.delete_calls.is_empty()).is_true()
	assert_that(out[0]).is_equal(OK)
