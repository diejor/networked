## Integration tests for [NetwDatabase] + [FileSystemBackend].
##
## These tests exercise the full persistence stack — schema registration,
## deferred backend initialization, transaction commits, disk reads, and
## schema-drift policy enforcement — all against real files in a temporary
## directory. No multiplayer peers are required.
##
## Tests that need the full multiplayer spawn chain (SaveComponent + database
## wired through a player scene) belong in a future test_save_with_database_flow.gd
## once a test scene with an exported NetwDatabase resource exists.
class_name TestDatabaseFlow
extends NetworkedTestSuite

var test_dir: String
var db: NetwDatabase
var backend: FileSystemBackend


func before_test() -> void:
	test_dir = create_temp_dir("database_flow_test")

	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir

	db = auto_free(NetwDatabase.new())
	db.backend = backend


# Helpers ───────────────────────────────────────────────────────────────────

func _register_and_wait(table: StringName, columns: Array[StringName]) -> void:
	db._register_schema(table, columns)
	await get_tree().process_frame  # let deferred _initialize_backend run


# ---------------------------------------------------------------------------
# Full save → load cycle
# ---------------------------------------------------------------------------

func test_save_flow_writes_to_backend() -> void:
	await _register_and_wait(&"players", [&"position", &"health"])

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"alice", {&"position": Vector2(10, 20), &"health": 100})
	)

	var record: Dictionary = db._find_by_id(&"players", &"alice")
	assert_that(record.get(&"position")).is_equal(Vector2(10, 20))
	assert_that(record.get(&"health")).is_equal(100)


func test_load_flow_reads_from_disk() -> void:
	await _register_and_wait(&"players", [&"position", &"health"])

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"bob", {&"position": Vector2(5, 15), &"health": 80})
	)

	# Null out the primary backend so the second one can use the same path.
	db = null
	backend = null
	FileSystemBackend._clear_path_registry()

	# Create a fresh database pointing at the same directory to simulate restart.
	var db2: NetwDatabase = auto_free(NetwDatabase.new())
	var backend2: FileSystemBackend = auto_free(FileSystemBackend.new())
	backend2.base_dir = test_dir
	db2.backend = backend2
	db2._register_schema(&"players", [&"position", &"health"])
	await get_tree().process_frame

	var record: Dictionary = db2._find_by_id(&"players", &"bob")
	assert_that(record.get(&"position")).is_equal(Vector2(5, 15))
	assert_that(record.get(&"health")).is_equal(80)


func test_two_entities_isolated() -> void:
	await _register_and_wait(&"players", [&"position", &"health"])

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"alice", {&"position": Vector2(1, 2), &"health": 100})
		tx.queue_upsert(&"players", &"bob",   {&"position": Vector2(9, 8), &"health": 50})
	)

	var alice: Dictionary = db._find_by_id(&"players", &"alice")
	var bob: Dictionary   = db._find_by_id(&"players", &"bob")

	assert_that(alice.get(&"position")).is_equal(Vector2(1, 2))
	assert_that(bob.get(&"position")).is_equal(Vector2(9, 8))


# ---------------------------------------------------------------------------
# Ghost table detection
# ---------------------------------------------------------------------------

func test_ghost_table_warning_on_initialize() -> void:
	# Pre-create a directory that is NOT declared in the schema.
	DirAccess.make_dir_recursive_absolute(test_dir.path_join("ghost_table"))

	# Only declare 'players' — 'ghost_table' should be flagged as a ghost.
	# push_warning is the observable side-effect; we verify _initialize returns OK.
	var err: Error = backend.initialize({&"players": [&"position"]})
	assert_that(err).is_equal(OK)


# ---------------------------------------------------------------------------
# Schema mismatch — PURGE policy (default)
# ---------------------------------------------------------------------------

func test_schema_mismatch_purge_in_full_flow() -> void:
	# Null out the primary test backend.
	db = null
	backend = null

	# === Schema v1: save a record with 'gold'. ===
	var backend_v1: FileSystemBackend = auto_free(FileSystemBackend.new())
	backend_v1.base_dir = test_dir
	var db_v1: NetwDatabase = auto_free(NetwDatabase.new())
	db_v1.backend = backend_v1
	db_v1.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.PURGE
	db_v1._register_schema(&"players", [&"health", &"gold"])
	await get_tree().process_frame

	db_v1.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"charlie", {&"health": 70, &"gold": 99})
	)

	# Null out v1 to allow v2 to take the path.
	db_v1 = null
	backend_v1 = null
	FileSystemBackend._clear_path_registry()

	# === Schema v2: 'gold' is gone. ===
	var backend_v2: FileSystemBackend = auto_free(FileSystemBackend.new())
	backend_v2.base_dir = test_dir
	var db_v2: NetwDatabase = auto_free(NetwDatabase.new())
	db_v2.backend = backend_v2
	db_v2.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.PURGE
	db_v2._register_schema(&"players", [&"health"])  # 'gold' removed
	await get_tree().process_frame

	var out_err: Array[int] = [OK]
	var record: Dictionary = db_v2._find_by_id(&"players", &"charlie", out_err)

	# PURGE signals ERR_FILE_NOT_FOUND ("clean slate") so spawn() can use spawner state.
	assert_that(out_err[0]).is_equal(ERR_FILE_NOT_FOUND)
	assert_that(record.is_empty()).is_true()

	# Subsequent load must return ERR_FILE_NOT_FOUND (record is truly gone).
	var out2: Array[int] = [OK]
	var after_purge: Dictionary = db_v2._find_by_id(&"players", &"charlie", out2)
	assert_that(out2[0]).is_equal(ERR_FILE_NOT_FOUND)
	assert_that(after_purge.is_empty()).is_true()


# ---------------------------------------------------------------------------
# Schema mismatch — LOAD_PARTIAL policy
# ---------------------------------------------------------------------------

func test_schema_mismatch_load_partial_preserves_known_columns() -> void:
	await _register_and_wait(&"items", [&"damage", &"rarity"])

	# Write a record with an extra legacy column 'old_stat'.
	backend.upsert(&"items", &"sword", {&"damage": 15, &"rarity": 3, &"old_stat": 99})

	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL

	var record: Dictionary = db._find_by_id(&"items", &"sword")
	assert_that(record.get(&"damage")).is_equal(15)
	assert_that(record.get(&"rarity")).is_equal(3)
	assert_that(record.has(&"old_stat")).is_false()


# ---------------------------------------------------------------------------
# Schema mismatch — FAIL policy
# ---------------------------------------------------------------------------

func test_schema_mismatch_fail_returns_err_and_keeps_record() -> void:
	await _register_and_wait(&"items", [&"damage"])

	backend.upsert(&"items", &"axe", {&"damage": 20, &"legacy_power": 5})

	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.FAIL

	var out_err: Array[int] = [OK]
	var record: Dictionary = db._find_by_id(&"items", &"axe", out_err)

	assert_that(out_err[0]).is_equal(ERR_UNCONFIGURED)
	assert_that(record.is_empty()).is_true()

	# Record should NOT have been deleted (FAIL just refuses to load).
	var raw: Dictionary = backend.find_by_id(&"items", &"axe")
	assert_that(raw.has(&"legacy_power")).is_true()


# ---------------------------------------------------------------------------
# Debugger signals emitted during save/load cycle
# ---------------------------------------------------------------------------

func test_record_loaded_signal_fires_on_find_by_id() -> void:
	await _register_and_wait(&"players", [&"health"])

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"diana", {&"health": 60})
	)

	var fired := [false]
	var hit_value := [false]
	db.record_loaded.connect(func(_t, _id, hit: bool):
		fired[0] = true
		hit_value[0] = hit
	)

	db._find_by_id(&"players", &"diana")

	assert_that(fired[0]).is_true()
	assert_that(hit_value[0]).is_true()


func test_record_loaded_hit_false_on_miss() -> void:
	await _register_and_wait(&"players", [&"health"])

	var hit_value := [true]
	db.record_loaded.connect(func(_t, _id, hit: bool): hit_value[0] = hit)

	db._find_by_id(&"players", &"nobody")

	assert_that(hit_value[0]).is_false()


func test_schema_mismatch_signal_fires_during_load() -> void:
	await _register_and_wait(&"players", [&"health"])

	backend.upsert(&"players", &"eve", {&"health": 40, &"old_col": 1})

	var mismatch_fired := [false]
	db.schema_mismatch.connect(func(_t, _id, _m, _u): mismatch_fired[0] = true)
	db.mismatch_policy = NetwDatabase.SchemaMismatchPolicy.LOAD_PARTIAL

	db._find_by_id(&"players", &"eve")

	assert_that(mismatch_fired[0]).is_true()


func test_transaction_committed_signal_fires_after_commit() -> void:
	await _register_and_wait(&"players", [&"health"])

	var record_count := [0]
	db.transaction_committed.connect(func(_tc, rc: int): record_count[0] = rc)

	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"players", &"frank", {&"health": 30})
		tx.queue_upsert(&"players", &"grace", {&"health": 25})
	)

	assert_that(record_count[0]).is_equal(2)


# ---------------------------------------------------------------------------
# Schema precondition — unregistered table must NOT purge records
# ---------------------------------------------------------------------------

func test_unregistered_table_read_does_not_purge_existing_record() -> void:
	await _register_and_wait(&"items", [&"damage"])

	# Seed a record.
	db.transaction(func(tx: NetwDatabase.TransactionContext):
		tx.queue_upsert(&"items", &"sword", {&"damage": 15})
	)

	# Verify record exists.
	var raw: Dictionary = db._find_by_id(&"items", &"sword")
	assert_that(raw.is_empty()).is_false()

	# Erase schema (simulating the bug: schema not registered at read time).
	db._schema.erase(&"items")

	# Fetching from an unregistered table returns null
	# and emits a warning, but does NOT touch the backend.
	var fetched := db.table(&"items").fetch(&"sword")
	assert_that(fetched).is_null()

	# Record is still on disk — it was NOT purged.
	var raw_after: Dictionary = backend.find_by_id(&"items", &"sword")
	assert_that(raw_after.is_empty()).is_false()
