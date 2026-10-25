class_name TestNakamaDatabaseLive
extends NetwTestSuite

const _ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)

var _tree: MultiplayerViewport
var _api: MultiplayerAPI
var _app := ""


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	_app = "netwlive%d%d" % [Time.get_ticks_usec(), randi() % 1000]
	_tree = auto_free(NakamaTestSupport.make_client_tree(self, "db"))
	_api = _tree.api
	var signed_in := await NakamaTestSupport.sign_in(
		_tree,
		"db-%d-%d" % [Time.get_ticks_usec(), randi()],
	)
	assert_int(signed_in) \
			.override_failure_message("Nakama sign in failed.").is_equal(OK)


func after_test() -> void:
	if NakamaTestSupport.account(_tree).session != null:
		var backend := _backend()
		var listing := backend._list_slots(_api)
		await listing.wait()
		if listing.get_is_completed():
			for slot in listing.get_result() as PackedStringArray:
				await backend._delete_slot(_api, StringName(slot)).wait()
	await super.after_test()


func _backend() -> NakamaDatabase:
	var made := NakamaDatabase.new()
	made.app = _app
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
	var promise := backend._open(_api, StringName(slot))
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


func test_a_record_and_a_snapshot_of_one_key_are_two_objects() -> void:
	var connection := await _open(_backend(), "slot1")
	var wrote := connection._write_batch(
		[
			{
				"kind": "replace",
				"address": _address(0, "hero"),
				"envelope": _envelope(1),
			},
			{
				"kind": "replace",
				"address": _address(1, "hero"),
				"envelope": _envelope(2),
			},
			{ "kind": "erase", "address": _address(0, "hero") },
		],
	)
	await wrote.wait()
	assert_array(Array(wrote.get_result()["errors"])).is_equal([OK, OK, OK])

	var record := connection._read(_address(0, "hero"))
	await record.wait()
	assert_bool(bool(record.get_result()["found"])).is_false()
	var snapshot := connection._read(_address(1, "hero"))
	await snapshot.wait()
	assert_bool(bool(snapshot.get_result()["found"])) \
			.override_failure_message(
				"Erasing a record erased the snapshot with the same key.",
			).is_true()


func test_a_foreign_value_is_found_and_unrecognized() -> void:
	var backend := _backend()
	var connection := await _open(backend, "slot1")
	var account := NakamaTestSupport.account(_tree)
	var legacy := JSON.stringify({ "gold": 4, "name": "hero" })
	await account.client.write_storage_objects_async(
		account.session,
		[
			NakamaWriteStorageObject.new(
				backend.collection_for("slot1"),
				NakamaDatabase.address_key(_address(0, "hero")),
				1,
				1,
				legacy,
				"",
			),
		],
	)

	var read := connection._read(_address(0, "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])).is_equal(OK)
	assert_bool(bool(reply["found"])) \
			.override_failure_message(
				"A stored value was read as an empty save.",
			).is_true()
	assert_dict(reply["envelope"]).is_empty()


func test_a_refused_session_fails_without_reading_as_absence() -> void:
	var backend := _backend()
	var signed_in := await _open(backend, "slot1")
	var wrote := signed_in._write_batch(
		[
			{
				"kind": "replace",
				"address": _address(0, "hero"),
				"envelope": _envelope(1),
			},
		],
	)
	await wrote.wait()

	var forged_tree: MultiplayerViewport = auto_free(
		NakamaTestSupport.make_client_tree(self, "forged"),
	)
	var forged := NakamaTestSupport.account(forged_tree)
	var claims := Marshalls.utf8_to_base64(
		JSON.stringify(
			{
				"exp": int(Time.get_unix_time_from_system()) + 3600,
				"uid": NakamaTestSupport.account(_tree).user_id,
				"usn": "forged",
			},
		),
	)
	forged.use_session(NakamaSession.new("e30.%s.c2ln" % claims))
	await forged.sign_in().wait()
	await _ERROR_SCRUBBER.erase_matching(["Invalid status code"])
	await _ERROR_SCRUBBER.erase_matching(["Invalid response headers"])
	var refused := NakamaDatabase.Connection.new(
		forged,
		backend.collection_for("slot1"),
	)

	var read := refused._read(_address(0, "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])) \
			.override_failure_message(
				"A read Nakama refused did not carry its error.",
			).is_equal(ERR_UNAUTHORIZED)
	assert_bool(bool(reply["found"])).is_false()

	var batch := refused._write_batch(
		[
			{
				"kind": "replace",
				"address": _address(0, "hero"),
				"envelope": _envelope(2),
			},
			{ "kind": "ponder", "address": _address(0, "two") },
		],
	)
	await batch.wait()
	var answer: Dictionary = batch.get_result()
	assert_int(int(answer["error"])).is_equal(OK)
	assert_array(Array(answer["errors"])) \
			.is_equal([ERR_UNAUTHORIZED, ERR_INVALID_DATA])
	assert_array(Array(answer["uncertain"])).is_equal([0, 0])


func test_a_scan_pages_real_storage_and_its_cursor_continues() -> void:
	var backend := _backend()
	var connection := await _open(backend, "slot1")

	var operations: Array = []
	for at in 2:
		operations.append(
			{
				"kind": "replace",
				"address": { "kind": 0, "schema_name": "others", "key": "o%d" % at },
				"envelope": _envelope(at),
			},
		)
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

	var listing := backend._list_slots(_api)
	await listing.wait()
	var names: PackedStringArray = listing.get_result()
	assert_bool(names.has("slot1")).is_true()
	assert_bool(names.has("slot2")).is_true()

	var removed := backend._delete_slot(_api, &"slot1")
	await removed.wait()
	assert_int(int(removed.get_result())).is_equal(OK)

	var after := backend._list_slots(_api)
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
