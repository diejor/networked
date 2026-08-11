## Tests for save-slot namespacing on [NetwDatabase] and [FileSystemDatabase].
##
## Covers the [NetwDatabase.SlotEngine] startup-only lock, slot path prefixing,
## per-slot isolation, and slot enumeration/deletion with no slot open.
class_name TestDatabaseSlots
extends NetwTestSuite

var test_dir: String


func before_test() -> void:
	test_dir = create_temp_dir("database_slots_test")


func after_test() -> void:
	FileSystemDatabase._clear_path_registry()
	await get_tree().process_frame
	await super.after_test()


func _make_db(slot: StringName) -> NetwDatabase:
	var backend: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	backend.base_dir = test_dir
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = backend
	db.warm_policy = null
	db.slots.open(slot)
	return db


func test_slot_selection_and_lock_flow() -> void:
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	assert_str(db.slots.current()).is_equal(&"default")

	db = _make_db(&"slot_a")
	assert_str(db.slots.current()).is_equal(&"slot_a")
	assert_bool(db.slots._locked).is_false()

	db._register_schema(&"players", [&"hp"])
	await get_tree().process_frame
	assert_bool(db.slots._locked).is_true()


func test_slot_isolates_records() -> void:
	var db_a := _make_db(&"slot_a")
	db_a._register_schema(&"players", [&"hp"])
	await get_tree().process_frame
	await db_a.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"p1", { &"hp": 42 })
	)

	var record_a: Dictionary = await db_a._find_by_id(&"players", &"p1")
	assert_int(record_a.get(&"hp")).is_equal(42)

	var db_b := _make_db(&"slot_b")
	db_b._register_schema(&"players", [&"hp"])
	await get_tree().process_frame
	var record_b: Dictionary = await db_b._find_by_id(&"players", &"p1")
	assert_bool(record_b.is_empty()).is_true()


func test_namespace_listing_deletion_and_registry_flow() -> void:
	var schema := { &"players": [] as Array[StringName] }

	var seed_a: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	seed_a.base_dir = test_dir
	assert_int(seed_a.initialize(schema, "slot_a")).is_equal(OK)
	seed_a.upsert(&"players", &"p1", { &"hp": 1 })

	var seed_b: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	seed_b.base_dir = test_dir
	assert_int(seed_b.initialize(schema, "slot_b")).is_equal(OK)
	seed_b.upsert(&"players", &"p2", { &"hp": 2 })
	seed_a.upsert(&"players", &"shared", { &"hp": 10 })
	seed_b.upsert(&"players", &"shared", { &"hp": 20 })

	assert_int(seed_a.find_by_id(&"players", &"shared").get(&"hp")).is_equal(10)
	assert_int(seed_b.find_by_id(&"players", &"shared").get(&"hp")).is_equal(20)

	var browser: FileSystemDatabase = auto_free(FileSystemDatabase.new())
	browser.base_dir = test_dir
	var listed: Array[StringName] = browser.list_namespaces()
	assert_array(listed).contains([&"slot_a", &"slot_b"])

	assert_int(browser.delete_namespace("slot_a")).is_equal(OK)
	var _after: Array[StringName] = browser.list_namespaces()
	assert_array(_after).not_contains([&"slot_a"])
	assert_array(_after).contains([&"slot_b"])
