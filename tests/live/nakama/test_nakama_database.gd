## Live persistence tests for [NakamaDatabase] against a Docker Nakama server.
##
## The unit and shape tests cover the cache, the merge invariant, and the wrapper
## storage surface offline. These exercise the real round trip: a write-behind
## upsert, a debounced flush to Nakama, and a cold read from a fresh backend that
## must hit storage. They also pin slot isolation and that a subset upsert
## preserves untouched columns through a flush. A unique
## [member NakamaDatabase.app_id] per run isolates the shared storage engine.
class_name TestNakamaDatabase
extends NetwTestSuite

const _TIMEOUT := 12.0

var _tree: MultiplayerTree
var _app_id := ""
var _backends: Array = []


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	_app_id = "netwdb-%d-%d" % [Time.get_ticks_usec(), randi()]

	# One tree + shared session: every backend resolves the same Nakama account.
	_tree = MultiplayerTree.new()
	_tree.name = "NakamaDbTree"
	_tree.auto_host_headless = false
	add_child(_tree)

	var session := NakamaSessionService.new()
	session.name = &"NakamaSession"
	_tree.add_child(session)
	session.host = NakamaTestServer.host()
	session.port = NakamaTestServer.DEFAULT_PORT
	session.use_ssl = false
	session.device_id = _app_id
	session.username = _app_id
	# Registered so get_nakama_session resolves this configured node, not a fresh
	# default one (find_service_node only matches scene-owned nodes).
	_tree.register_service(session)

	await drain_frames(get_tree(), 2)


func after_test() -> void:
	for backend in _backends:
		if backend != null:
			backend.stop()
	_backends.clear()
	if is_instance_valid(_tree):
		_tree.queue_free()
	await drain_frames(get_tree(), 3)
	await super.after_test()


func _make_db(slot: StringName) -> NetwDatabase:
	var backend := NakamaDatabase.new()
	backend.app_id = _app_id
	backend.flush_interval = 0.5
	_backends.append(backend)

	var db := NetwDatabase.new()
	db.backend = backend
	# Drive warming explicitly per test, so reads exercise the lazy fetch path.
	db.warm_policy = null
	db.slots.open(slot)
	db.declare_table(&"players", [&"hp", &"pname"])
	await get_tree().process_frame
	return db


func test_upsert_flush_then_cold_read_round_trips() -> void:
	var db := await _make_db(&"slot_a")
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"p1", { &"hp": 7, &"pname": "valeria" })
	)
	assert_int(await (db.backend as NakamaDatabase).drain()).is_equal(OK)

	# A fresh backend has a cold cache and must read from Nakama.
	var db2 := await _make_db(&"slot_a")
	var record: Dictionary = await db2._find_by_id(&"players", &"p1")
	assert_int(record.get(&"hp")).is_equal(7)
	assert_str(String(record.get(&"pname"))).is_equal("valeria")


func test_subset_upsert_preserves_untouched_columns() -> void:
	var db := await _make_db(&"slot_a")
	var backend := db.backend as NakamaDatabase
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"p2", { &"hp": 1, &"pname": "jose" })
	)
	assert_int(await backend.drain()).is_equal(OK)

	# Subset upsert: the cache still holds pname, so the next flush keeps it.
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"p2", { &"hp": 99 })
	)
	assert_int(await backend.drain()).is_equal(OK)

	var db2 := await _make_db(&"slot_a")
	var record: Dictionary = await db2._find_by_id(&"players", &"p2")
	assert_int(record.get(&"hp")).is_equal(99)
	assert_str(String(record.get(&"pname"))).is_equal("jose")


func test_delete_removes_record() -> void:
	var db := await _make_db(&"slot_a")
	var backend := db.backend as NakamaDatabase
	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"p3", { &"hp": 3 })
	)
	assert_int(await backend.drain()).is_equal(OK)

	await db.delete(&"players", &"p3")
	assert_int(await backend.drain()).is_equal(OK)

	var db2 := await _make_db(&"slot_a")
	var record: Dictionary = await db2._find_by_id(&"players", &"p3")
	assert_bool(record.is_empty()).is_true()


func test_slots_do_not_see_each_other() -> void:
	var db_a := await _make_db(&"slot_a")
	await db_a.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"shared", { &"hp": 5 })
	)
	assert_int(await (db_a.backend as NakamaDatabase).drain()).is_equal(OK)

	var db_b := await _make_db(&"slot_b")
	var record: Dictionary = await db_b._find_by_id(&"players", &"shared")
	assert_bool(record.is_empty()).is_true()
