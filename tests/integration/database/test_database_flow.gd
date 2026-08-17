## Integration tests for [NetwDatabase] and [FileSystemDatabase].
##
## Exercises the full persistence stack including schema registration,
## transactions, and drift policy enforcement.
class_name TestDatabaseFlow
extends NetwTestSuite

var test_dir: String
var db: NetwDatabase
var backend: FileSystemDatabase


func before_test() -> void:
	test_dir = create_temp_dir("database_flow_test")

	backend = auto_free(FileSystemDatabase.new())
	backend.base_dir = test_dir

	db = auto_free(NetwDatabase.new())
	db.backend = backend


func after_test() -> void:
	db = null
	backend = null
	FileSystemDatabase._clear_path_registry()
	await get_tree().process_frame
	await super.after_test()


func test_persistence_round_trip_flow() -> void:
	await _register_and_wait(&"players", [&"position", &"health"])

	var commit_count := [0]
	db.transaction_committed.connect(func(_tc, rc: int): commit_count[0] = rc)

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(
				&"players",
				&"valeria",
				{
					&"position": Vector2(10, 20),
					&"health": 100,
				},
			)
			tx.queue_upsert(
				&"players",
				&"jose",
				{
					&"position": Vector2(5, 15),
					&"health": 80,
				},
			)
	)

	assert_that(commit_count[0]).is_equal(2)
	var valeria: Dictionary = await db._find_by_id(&"players", &"valeria")
	var jose: Dictionary = await db._find_by_id(&"players", &"jose")
	assert_that(valeria.get(&"position")).is_equal(Vector2(10, 20))
	assert_that(valeria.get(&"health")).is_equal(100)
	assert_that(jose.get(&"position")).is_equal(Vector2(5, 15))
	assert_that(jose.get(&"health")).is_equal(80)

	db = null
	backend = null
	FileSystemDatabase._clear_path_registry()

	var db2: NetwDatabase = auto_free(NetwDatabase.new())
	var backend2: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	backend2.base_dir = test_dir
	db2.backend = backend2
	db2._register_schema(&"players", [&"position", &"health"])
	await get_tree().process_frame

	var cold: Dictionary = await db2._find_by_id(&"players", &"jose")
	assert_that(cold.get(&"position")).is_equal(Vector2(5, 15))
	assert_that(cold.get(&"health")).is_equal(80)


func test_schema_mismatch_policies_flow() -> void:
	await _assert_purge_policy()
	await _assert_load_partial_policy()
	await _assert_fail_policy_keeps_record()


func test_load_signals_flow() -> void:
	await _register_and_wait(&"players", [&"health"])

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"players", &"diana", { &"health": 60 })
	)

	var loaded_hits: Array[bool] = []
	db.record_loaded.connect(func(_t, _id, hit: bool): loaded_hits.append(hit))
	await db._find_by_id(&"players", &"diana")
	await db._find_by_id(&"players", &"nobody")
	assert_that(loaded_hits).contains_exactly([true, false])

	var mismatch_fired := [false]
	db.schema_mismatch.connect(func(_t, _id, _m, _u): mismatch_fired[0] = true)
	backend.upsert(&"players", &"eve", { &"health": 40, &"old_col": 1 })
	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL
	await db._find_by_id(&"players", &"eve")
	assert_that(mismatch_fired[0]).is_true()


func test_unregistered_table_read_is_non_destructive() -> void:
	await _register_and_wait(&"items", [&"damage"])

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"items", &"sword", { &"damage": 15 })
	)

	var raw: Dictionary = await db._find_by_id(&"items", &"sword")
	assert_that(raw.is_empty()).is_false()

	db._schema.erase(&"items")

	var fetched := await db.table(&"items").fetch(&"sword")
	assert_that(fetched).is_null()

	var raw_after: Dictionary = await NetwDatabase.settled_value(
		backend.find_by_id(&"items", &"sword"),
		{ },
	)
	assert_that(raw_after.is_empty()).is_false()

	DirAccess.make_dir_recursive_absolute(test_dir.path_join("ghost_table"))
	var err: Error = await NetwDatabase.settled_error(
		backend.initialize({ &"items": [&"damage"] }),
	)
	assert_that(err).is_equal(OK)


func _register_and_wait(table: StringName, columns: Array[StringName]) -> void:
	db._register_schema(table, columns)
	await get_tree().process_frame


func _assert_purge_policy() -> void:
	db = null
	backend = null
	FileSystemDatabase._clear_path_registry()
	var purge_dir := test_dir.path_join("purge")

	var backend_v1: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	backend_v1.base_dir = purge_dir
	var db_v1: NetwDatabase = auto_free(NetwDatabase.new())
	db_v1.backend = backend_v1
	db_v1.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.PURGE
	db_v1._register_schema(&"players", [&"health", &"gold"])
	await get_tree().process_frame

	await db_v1.transaction(
		func(tx: NetwDatabase.TransactionContext):
			tx.queue_upsert(&"players", &"charlie", { &"health": 70, &"gold": 99 })
	)

	db_v1 = null
	backend_v1 = null
	FileSystemDatabase._clear_path_registry()

	var backend_v2: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	backend_v2.base_dir = purge_dir
	var db_v2: NetwDatabase = auto_free(NetwDatabase.new())
	db_v2.backend = backend_v2
	db_v2.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.PURGE
	db_v2._register_schema(&"players", [&"health"])
	await get_tree().process_frame

	var out_err: Array[int] = [OK]
	var record: Dictionary = await db_v2._find_by_id(
		&"players",
		&"charlie",
		out_err,
	)
	assert_that(out_err[0]).is_equal(ERR_FILE_NOT_FOUND)
	assert_that(record.is_empty()).is_true()


func _assert_load_partial_policy() -> void:
	db = auto_free(NetwDatabase.new())
	backend = auto_free(FileSystemDatabase.new())
	FileSystemDatabase._clear_path_registry()
	backend.base_dir = test_dir.path_join("partial")
	db.backend = backend
	await _register_and_wait(&"items", [&"damage", &"rarity"])

	backend.upsert(
		&"items",
		&"sword",
		{
			&"damage": 15,
			&"rarity": 3,
			&"old_stat": 99,
		},
	)

	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL

	var record: Dictionary = await db._find_by_id(&"items", &"sword")
	assert_that(record.get(&"damage")).is_equal(15)
	assert_that(record.get(&"rarity")).is_equal(3)
	assert_that(record.has(&"old_stat")).is_false()


func _assert_fail_policy_keeps_record() -> void:
	db = auto_free(NetwDatabase.new())
	backend = auto_free(FileSystemDatabase.new())
	FileSystemDatabase._clear_path_registry()
	backend.base_dir = test_dir.path_join("fail")
	db.backend = backend
	await _register_and_wait(&"gear", [&"damage"])

	backend.upsert(&"gear", &"axe", { &"damage": 20, &"legacy_power": 5 })

	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.FAIL

	var out_err: Array[int] = [OK]
	var record: Dictionary = await db._find_by_id(&"gear", &"axe", out_err)

	assert_that(out_err[0]).is_equal(ERR_UNCONFIGURED)
	assert_that(record.is_empty()).is_true()

	var raw: Dictionary = await NetwDatabase.settled_value(
		backend.find_by_id(&"gear", &"axe"),
		{ },
	)
	assert_that(raw.has(&"legacy_power")).is_true()
