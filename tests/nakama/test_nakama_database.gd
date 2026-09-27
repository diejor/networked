class_name TestNakamaDatabase
extends NetwTestSuite

const APP := "netw_laws"
const SLOT := "slot1"


class FakeNakama extends RefCounted:
	signal released()

	var rows: Dictionary = { }
	var user := "user-1"
	var page_size := 100
	var hold := false
	var forced: Dictionary = { }
	var calls: PackedStringArray = []


	func own_user_id() -> String:
		return user


	func fail_next(verb: String, answer: Dictionary) -> void:
		forced[verb] = answer


	func stored(collection: String) -> Dictionary:
		return rows.get(collection, { })


	func plant(collection: String, key: String, value: String) -> void:
		if not rows.has(collection):
			rows[collection] = { }
		rows[collection][key] = value


	func _taken(verb: String) -> Dictionary:
		if not forced.has(verb):
			return { }
		var answer: Dictionary = forced[verb]
		forced.erase(verb)
		return answer


	func write_storage_objects(objects: Array) -> Dictionary:
		calls.append("write")
		if hold:
			await released
		var refused := _taken("write")
		if not refused.is_empty():
			return refused
		for entry in objects:
			plant(
				String(entry["collection"]),
				String(entry["key"]),
				String(entry["value"]),
			)
		return NakamaWrapper.storage_answer(OK, "", false)


	func read_storage_objects(ids: Array) -> Dictionary:
		calls.append("read")
		var refused := _taken("read")
		if not refused.is_empty():
			refused["objects"] = []
			return refused
		var out: Array = []
		for id in ids:
			var collection := String(id["collection"])
			var key := String(id["key"])
			if stored(collection).has(key):
				out.append(
					{
						"collection": collection,
						"key": key,
						"user_id": user,
						"value": JSON.parse_string(stored(collection)[key]),
					},
				)
		var answer := NakamaWrapper.storage_answer(OK, "", false)
		answer["objects"] = out
		return answer


	func list_storage_objects(
			collection: String,
			limit := 100,
			cursor := "",
			owner := "",
	) -> Dictionary:
		calls.append("list")
		var refused := _taken("list")
		if not refused.is_empty():
			refused["objects"] = []
			refused["cursor"] = ""
			return refused
		var keys := PackedStringArray(stored(collection).keys())
		keys.sort()
		var take := mini(limit, page_size)
		var out: Array = []
		var last := ""
		var remaining := false
		for key in keys:
			if not cursor.is_empty() and key <= cursor:
				continue
			if out.size() >= take:
				remaining = true
				break
			out.append(
				{
					"key": key,
					"user_id": user,
					"value": JSON.parse_string(stored(collection)[key]),
				},
			)
			last = key
		var answer := NakamaWrapper.storage_answer(OK, "", false)
		answer["objects"] = out
		answer["cursor"] = last if remaining else ""
		return answer


	func delete_storage_objects(ids: Array) -> Dictionary:
		calls.append("delete")
		var refused := _taken("delete")
		if not refused.is_empty():
			return refused
		for id in ids:
			var collection := String(id["collection"])
			if rows.has(collection):
				rows[collection].erase(String(id["key"]))
		return NakamaWrapper.storage_answer(OK, "", false)


class FakeException extends RefCounted:
	var message := ""
	var status_code := -1
	var cancelled := false


	func _init(p_message: String, p_status: int, p_cancelled := false) -> void:
		message = p_message
		status_code = p_status
		cancelled = p_cancelled


func backend_over(fake: FakeNakama) -> NakamaDatabase:
	var made := NakamaDatabase.new()
	made.app = APP
	made.wrapper = fake
	return made


func address(kind: int, schema_name: String, key: String) -> Dictionary:
	return { "kind": kind, "schema_name": schema_name, "key": key }


func envelope(gold: int) -> Dictionary:
	return {
		"format_version": 1,
		"kind": 0,
		"schema_name": "players",
		"schema_version": 1,
		"payload": { "gold": gold, "where": Vector2(1, 2) },
	}


func replacement(kind: int, key: String, gold: int) -> Dictionary:
	return {
		"kind": "replace",
		"address": address(kind, "players", key),
		"envelope": envelope(gold),
	}


func opened(backend: NakamaDatabase) -> NetwDatabaseConnection:
	var promise := backend._open(null, SLOT)
	await promise.wait()
	return promise.get_result() as NetwDatabaseConnection


func test_a_write_is_acknowledged_only_after_the_service_answered() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	fake.hold = true
	var promise := connection._write_batch([replacement(0, "hero", 5)])
	await get_tree().process_frame
	assert_bool(promise.get_is_settled()) \
			.override_failure_message(
				"The batch acknowledged before Nakama answered.",
			).is_false()

	fake.released.emit()
	await promise.wait()
	assert_bool(promise.get_is_completed()).is_true()
	var errors: PackedInt32Array = promise.get_result()["errors"]
	assert_int(errors[0]).is_equal(OK)
	assert_bool(fake.stored(backend.collection_for(SLOT)).has("0_players_hero")) \
			.is_true()


func test_a_record_round_trips_and_absence_is_not_a_failure() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	var wrote := connection._write_batch([replacement(0, "hero", 7)])
	await wrote.wait()

	var read := connection._read(address(0, "players", "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])).is_equal(OK)
	assert_bool(bool(reply["found"])).is_true()
	var held: Dictionary = reply["envelope"]
	assert_int(int(held["payload"]["gold"])).is_equal(7)
	assert_vector(held["payload"]["where"] as Vector2).is_equal(Vector2(1, 2))

	var missing := connection._read(address(0, "players", "ghost"))
	await missing.wait()
	var absent: Dictionary = missing.get_result()
	assert_int(int(absent["error"])).is_equal(OK)
	assert_bool(bool(absent["found"])).is_false()
	assert_bool(absent.has("envelope")).is_false()


func test_a_read_failure_carries_the_service_error_not_absence() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	fake.fail_next(
		"read",
		NakamaWrapper.storage_answer(ERR_UNAUTHORIZED, "session expired", false),
	)
	var read := connection._read(address(0, "players", "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])).is_equal(ERR_UNAUTHORIZED)
	assert_bool(bool(reply["found"])).is_false()
	assert_str(String(reply["detail"])).is_equal("session expired")

	fake.fail_next(
		"read",
		NakamaWrapper.storage_answer(ERR_INVALID_DATA, "rejected", false),
	)
	var rejected := connection._read(address(0, "players", "hero"))
	await rejected.wait()
	assert_int(int(rejected.get_result()["error"])).is_equal(ERR_INVALID_DATA)


func test_a_scan_pages_remotely_and_an_empty_page_is_not_exhaustion() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	var wrote := connection._write_batch(
		[
			{
				"kind": "replace",
				"address": address(0, "others", "a"),
				"envelope": envelope(1),
			},
			replacement(0, "z", 2),
		],
	)
	await wrote.wait()
	fake.page_size = 1

	var request := {
		"schema_name": "players",
		"kind": 0,
		"cursor": "",
		"limit": 100,
	}
	var first := connection._scan(request)
	await first.wait()
	var page: Dictionary = first.get_result()
	assert_int(int(page["error"])).is_equal(OK)
	assert_array(page["records"]) \
			.override_failure_message(
				"The page holding only another schema answered records.",
			).is_empty()
	assert_str(String(page["cursor"])) \
			.override_failure_message(
				"An unexhausted scan answered no cursor, so paging stops early.",
			).is_not_empty()

	request["cursor"] = page["cursor"]
	var second := connection._scan(request)
	await second.wait()
	var rest: Dictionary = second.get_result()
	assert_int((rest["records"] as Array).size()).is_equal(1)
	assert_str(String((rest["records"] as Array)[0]["key"])).is_equal("z")
	assert_str(String(rest["cursor"])).is_empty()


func test_a_record_and_a_snapshot_of_one_key_are_two_objects() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	var wrote := connection._write_batch(
		[replacement(0, "hero", 1), replacement(1, "hero", 2)],
	)
	await wrote.wait()
	assert_int(fake.stored(backend.collection_for(SLOT)).size()).is_equal(2)

	var erased := connection._write_batch(
		[{ "kind": "erase", "address": address(0, "players", "hero") }],
	)
	await erased.wait()

	var record := connection._read(address(0, "players", "hero"))
	await record.wait()
	assert_bool(bool(record.get_result()["found"])).is_false()
	var snapshot := connection._read(address(1, "players", "hero"))
	await snapshot.wait()
	assert_bool(bool(snapshot.get_result()["found"])).is_true()

	assert_str(NakamaDatabase.address_key(address(0, "players", "a/b"))) \
			.is_not_equal(NakamaDatabase.address_key(address(0, "players/a", "b")))
	assert_str(NakamaDatabase.address_key(address(0, "p", ".."))) \
			.is_equal("0_p_%2E%2E")


func test_a_batch_reports_one_honest_outcome_per_operation() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)

	fake.fail_next(
		"write",
		NakamaWrapper.storage_answer(ERR_TIMEOUT, "no answer", true),
	)
	var promise := connection._write_batch(
		[
			replacement(0, "one", 1),
			{ "kind": "ponder", "address": address(0, "players", "two") },
			replacement(0, "three", 3),
		],
	)
	await promise.wait()
	var reply: Dictionary = promise.get_result()
	assert_int(int(reply["error"])).is_equal(OK)
	var errors: PackedInt32Array = reply["errors"]
	var uncertain: PackedByteArray = reply["uncertain"]
	assert_int(errors.size()).is_equal(3)
	assert_int(errors[0]).is_equal(ERR_TIMEOUT)
	assert_int(errors[1]).is_equal(ERR_INVALID_DATA)
	assert_int(errors[2]).is_equal(OK)
	assert_int(uncertain[0]) \
			.override_failure_message(
				"An outcome the service never established was reported known.",
			).is_equal(1)
	assert_int(uncertain[1]).is_equal(0)
	assert_int(uncertain[2]).is_equal(0)

	var store := fake.stored(backend.collection_for(SLOT))
	assert_bool(store.has("0_players_one")).is_false()
	assert_bool(store.has("0_players_three")).is_true()


func test_opening_lists_the_slot_before_any_record_is_written() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)

	fake.hold = true
	var opening := backend._open(null, SLOT)
	await get_tree().process_frame
	assert_bool(opening.get_is_settled()) \
			.override_failure_message(
				"A connection was handed out before its manifest entry landed.",
			).is_false()
	assert_bool(fake.stored(APP).is_empty()).is_true()
	fake.released.emit()
	await opening.wait()
	fake.hold = false

	var connection := opening.get_result() as NetwDatabaseConnection
	assert_bool(fake.stored(APP).has(NakamaDatabase.encoded(SLOT))) \
			.override_failure_message(
				"A slot was opened without its manifest entry.",
			).is_true()
	assert_bool(fake.stored(backend.collection_for(SLOT)).is_empty()).is_true()

	var wrote := connection._write_batch([replacement(0, "hero", 1)])
	await wrote.wait()

	fake.page_size = 1
	await backend._open(null, "a slot/with punctuation").wait()
	var listing := backend._list_slots(null)
	await listing.wait()
	var names: PackedStringArray = listing.get_result()
	assert_bool(names.has(SLOT)).is_true()
	assert_bool(names.has("a slot/with punctuation")) \
			.override_failure_message(
				"Slot listing stopped at the first remote page.",
			).is_true()


func test_deleting_a_slot_drains_every_page_then_the_manifest() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)
	await backend._open(null, "keep").wait()

	var wrote := connection._write_batch(
		[
			replacement(0, "one", 1),
			{
				"kind": "replace",
				"address": address(0, "others", "two"),
				"envelope": envelope(2),
			},
			replacement(1, "three", 3),
		],
	)
	await wrote.wait()
	var kept := (await opened(backend))._write_batch([replacement(0, "x", 9)])
	await kept.wait()
	fake.page_size = 1

	var removed := backend._delete_slot(null, SLOT)
	await removed.wait()
	assert_int(int(removed.get_result())).is_equal(OK)
	assert_bool(fake.stored(backend.collection_for(SLOT)).is_empty()) \
			.override_failure_message(
				"Deleting a slot left records behind on a later page.",
			).is_true()
	assert_bool(fake.stored(APP).has(NakamaDatabase.encoded(SLOT))).is_false()
	assert_bool(fake.stored(APP).has(NakamaDatabase.encoded("keep"))).is_true()


func test_a_foreign_value_is_found_and_unrecognized() -> void:
	var fake := FakeNakama.new()
	var backend := backend_over(fake)
	var connection := await opened(backend)
	var legacy := JSON.stringify({ "gold": 4, "name": "hero" })
	fake.plant(backend.collection_for(SLOT), "0_players_hero", legacy)

	var read := connection._read(address(0, "players", "hero"))
	await read.wait()
	var reply: Dictionary = read.get_result()
	assert_int(int(reply["error"])).is_equal(OK)
	assert_bool(bool(reply["found"])) \
			.override_failure_message(
				"A stored value was read as an empty save.",
			).is_true()
	assert_dict(reply["envelope"]).is_empty()
	assert_str(fake.stored(backend.collection_for(SLOT))["0_players_hero"]) \
			.is_equal(legacy)


func test_a_service_fault_keeps_auth_rejection_and_timeout_apart() -> void:
	var silent := NakamaWrapper._storage_fault(FakeException.new("gone", -1))
	assert_int(int(silent["error"])).is_equal(ERR_TIMEOUT)
	assert_bool(bool(silent["uncertain"])) \
			.override_failure_message(
				"An outcome the service never reported was called known.",
			).is_true()

	var denied := NakamaWrapper._storage_fault(FakeException.new("token", 401))
	assert_int(int(denied["error"])).is_equal(ERR_UNAUTHORIZED)
	assert_bool(bool(denied["uncertain"])).is_false()

	var rejected := NakamaWrapper._storage_fault(FakeException.new("bad", 400))
	assert_int(int(rejected["error"])).is_equal(ERR_INVALID_DATA)
	assert_bool(bool(rejected["uncertain"])).is_false()

	var broke := NakamaWrapper._storage_fault(FakeException.new("boom", 503))
	assert_int(int(broke["error"])).is_equal(ERR_CONNECTION_ERROR)
	assert_bool(bool(broke["uncertain"])).is_true()

	var stopped := NakamaWrapper._storage_fault(
		FakeException.new("cancelled", 200, true),
	)
	assert_int(int(stopped["error"])).is_equal(ERR_TIMEOUT)
	assert_bool(bool(stopped["uncertain"])).is_true()


func test_a_backend_with_no_wrapper_refuses_every_verb() -> void:
	var backend := NakamaDatabase.new()
	backend.app = APP

	var opening := backend._open(null, SLOT)
	await opening.wait()
	assert_bool(opening.get_is_failed()).is_true()
	assert_int(opening.get_code()).is_equal(ERR_UNCONFIGURED)

	var listing := backend._list_slots(null)
	await listing.wait()
	assert_bool(listing.get_is_failed()).is_true()

	var fake := FakeNakama.new()
	var bound := backend_over(fake)
	fake.fail_next(
		"list",
		NakamaWrapper.storage_answer(ERR_UNAUTHORIZED, "not signed in", false),
	)
	var refused := bound._list_slots(null)
	await refused.wait()
	assert_bool(refused.get_is_failed()).is_true()
	assert_int(refused.get_code()).is_equal(ERR_UNAUTHORIZED)
