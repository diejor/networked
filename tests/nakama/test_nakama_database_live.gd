class_name TestNakamaDatabaseLive
extends NetwTestSuite

var _service: NakamaSessionService
var _app := ""


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	_app = "netwlive%d%d" % [Time.get_ticks_usec(), randi() % 1000]
	_service = auto_free(NakamaSessionService.new())
	add_child(_service)
	_service.configure(
		{
			"host": NakamaTestServer.host(),
			"port": NakamaTestServer.DEFAULT_PORT,
			"use_ssl": false,
			"server_key": "defaultkey",
			"device_id": "netw-db-%d-%d" % [Time.get_ticks_usec(), randi()],
			"username": "netwdb%d" % (randi() % 100000),
		},
	)
	var auth: Dictionary = await _service.connect_async()
	assert_bool(bool(auth["ok"])) \
			.override_failure_message(
				"Nakama authentication failed: %s" % auth["error"],
			).is_true()


func after_test() -> void:
	if _service != null and _service.is_authenticated():
		var backend := _backend()
		var listing := backend._list_slots(null)
		await listing.wait()
		if listing.get_is_completed():
			for slot in listing.get_result() as PackedStringArray:
				await backend._delete_slot(null, StringName(slot)).wait()
	await super.after_test()


func _wrapper() -> NakamaWrapper:
	var made := NakamaWrapper.new()
	made.use_session(_service)
	return made


func _backend() -> NakamaDatabase:
	var made := NakamaDatabase.new()
	made.app = _app
	made.wrapper = _wrapper()
	return made


func _envelope(gold: int) -> Dictionary:
	return {
		"format_version": 1,
		"kind": 0,
		"schema_name": "players",
		"schema_version": 1,
		"payload": { "gold": gold, "where": Vector2(3, 4) },
	}


func _address(kind: int, key: String) -> Dictionary:
	return { "kind": kind, "schema_name": "players", "key": key }


func _open(backend: NakamaDatabase, slot: String) -> NetwDatabaseConnection:
	var promise := backend._open(null, StringName(slot))
	await promise.wait()
	assert_bool(promise.get_is_completed()) \
			.override_failure_message(
				"Opening the slot failed: %s" % promise.get_detail(),
			).is_true()
	return promise.get_result() as NetwDatabaseConnection


func test_a_record_survives_the_connection_that_wrote_it() -> void:
	var backend := _backend()
	var connection := await _open(backend, "slot1")

	var wrote := connection._write_batch(
		[
			{
				"kind": "replace",
				"address": _address(0, "hero"),
				"envelope": _envelope(42),
			},
		],
	)
	await wrote.wait()
	var errors: PackedInt32Array = wrote.get_result()["errors"]
	assert_int(errors[0]) \
			.override_failure_message("Nakama refused the write.").is_equal(OK)

	var fresh := await _open(_backend(), "slot1")
	var read := fresh._read(_address(0, "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])).is_equal(OK)
	assert_bool(bool(reply["found"])) \
			.override_failure_message(
				"A record Nakama acknowledged was not there on a new connection.",
			).is_true()
	var held: Dictionary = reply["envelope"]
	assert_int(int(held["payload"]["gold"])).is_equal(42)
	assert_vector(held["payload"]["where"] as Vector2).is_equal(Vector2(3, 4))

	var missing := fresh._read(_address(0, "ghost"))
	await missing.wait()
	assert_int(int(missing.get_result()["error"])).is_equal(OK)
	assert_bool(bool(missing.get_result()["found"])).is_false()


func test_a_scan_pages_real_storage_and_its_cursor_continues() -> void:
	var backend := _backend()
	var connection := await _open(backend, "slot1")

	var operations: Array = []
	for at in 5:
		operations.append(
			{
				"kind": "replace",
				"address": _address(0, "p%d" % at),
				"envelope": _envelope(at),
			},
		)
	var wrote := connection._write_batch(operations)
	await wrote.wait()

	var seen := PackedStringArray()
	var cursor := ""
	var pages := 0
	while pages < 10:
		pages += 1
		var scan := connection._scan(
			{
				"schema_name": "players",
				"kind": 0,
				"cursor": cursor,
				"limit": 2,
			},
		)
		await scan.wait()
		var page: Dictionary = scan.get_result()
		assert_int(int(page["error"])).is_equal(OK)
		for row in page["records"] as Array:
			seen.append(String(row["key"]))
		cursor = String(page["cursor"])
		if cursor.is_empty():
			break
	assert_bool(cursor.is_empty()) \
			.override_failure_message("The scan never exhausted.").is_true()
	assert_int(pages) \
			.override_failure_message("A limit of 2 over 5 records took one page.") \
			.is_greater(1)
	seen.sort()
	assert_array(Array(seen)).is_equal(["p0", "p1", "p2", "p3", "p4"])


func test_a_slot_is_listed_then_deleted_with_every_schema_under_it() -> void:
	var backend := _backend()
	var connection := await _open(backend, "slot1")
	await _open(backend, "slot2")

	var wrote := connection._write_batch(
		[
			{
				"kind": "replace",
				"address": _address(0, "hero"),
				"envelope": _envelope(1),
			},
			{
				"kind": "replace",
				"address": {
					"kind": 0,
					"schema_name": "worlds",
					"key": "overworld",
				},
				"envelope": _envelope(2),
			},
			{
				"kind": "replace",
				"address": _address(1, "hero"),
				"envelope": _envelope(3),
			},
		],
	)
	await wrote.wait()

	var listing := backend._list_slots(null)
	await listing.wait()
	var names: PackedStringArray = listing.get_result()
	assert_bool(names.has("slot1")).is_true()
	assert_bool(names.has("slot2")).is_true()

	var removed := backend._delete_slot(null, &"slot1")
	await removed.wait()
	assert_int(int(removed.get_result())).is_equal(OK)

	var after := backend._list_slots(null)
	await after.wait()
	var left: PackedStringArray = after.get_result()
	assert_bool(left.has("slot1")) \
			.override_failure_message("A deleted slot is still listed.").is_false()
	assert_bool(left.has("slot2")).is_true()

	var reopened := await _open(_backend(), "slot1")
	for address in [
		_address(0, "hero"),
		{ "kind": 0, "schema_name": "worlds", "key": "overworld" },
		_address(1, "hero"),
	]:
		var read := reopened._read(address)
		await read.wait()
		assert_bool(bool(read.get_result()["found"])) \
				.override_failure_message(
					"Deleting the slot left %s behind." % [address],
				).is_false()
