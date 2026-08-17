## Laws for a backend that answers asynchronously.
##
## A backend is allowed to await, because a real one talks to a service over a
## socket. What that costs the contract is one object: every verb answers a
## [NetwPromise] straight away and settles it later, and the caller waits on
## that rather than on the call. A verb that answered the value directly would
## coerce a suspended GDScript call to an empty one, and the read would report
## a miss with no error anywhere: the shape this suite exists to keep out.
class_name TestAsyncBackendContract
extends NetwTestSuite


func _backend() -> TestAsyncBackend:
	return auto_free(TestAsyncBackend.new(get_tree()))


func test_an_awaiting_upsert_and_read_round_trip() -> void:
	var backend := _backend()

	var err: Error = await NetwDatabase.settled_error(
		backend.upsert(&"rocks", &"r1", { &"health": 50 }),
	)
	assert_that(err).is_equal(OK)

	var record: Dictionary = await NetwDatabase.settled_value(
		backend.find_by_id(&"rocks", &"r1"),
		{ },
	)
	assert_bool(record.is_empty()).override_failure_message(
		"an awaiting read answers with its record, or the suspended call was "
		+ "coerced to an empty one and the miss is indistinguishable from a "
		+ "record that is not there",
	).is_false()
	assert_that(record.get(&"health")).is_equal(50)


func test_an_awaiting_backend_round_trips_through_the_database() -> void:
	var backend := _backend()
	var db: NetwDatabase = auto_free(NetwDatabase.new())
	db.backend = backend
	db.warm_policy = null
	db.declare_table(&"rocks", [&"health"])

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"rocks", &"r1", { &"health": 50 })
	)

	var record: Dictionary = await db._find_by_id(&"rocks", &"r1")
	assert_that(record.get(&"health")).override_failure_message(
		"the database reads through an awaiting backend, which is every "
		+ "backend that stores anywhere but this process",
	).is_equal(50)


func test_an_awaiting_delete_removes_the_record() -> void:
	var backend := _backend()
	await NetwDatabase.settled_error(
		backend.upsert(&"rocks", &"r1", { &"health": 50 }),
	)

	var err: Error = await NetwDatabase.settled_error(
		backend.erase(&"rocks", &"r1"),
	)
	assert_that(err).is_equal(OK)

	var record: Dictionary = await NetwDatabase.settled_value(
		backend.find_by_id(&"rocks", &"r1"),
		{ },
	)
	assert_bool(record.is_empty()).is_true()


func test_an_awaiting_namespace_listing_answers() -> void:
	var backend := _backend()

	var names: Array[StringName] = await NetwDatabase.settled_value(
		backend.list_namespaces(),
		[] as Array[StringName],
	)
	assert_int(names.size()).override_failure_message(
		"a suspended listing answers with its names, or a slot picker reads "
		+ "an empty world and offers nothing",
	).is_equal(1)


## Verify the promise an awaiting backend answers with is NOT settled when the
## call returns, which is the whole of what "asynchronous" means here and the
## one fact a synchronous backend cannot also satisfy.
func test_an_awaiting_verb_answers_a_promise_that_has_not_settled_yet() -> void:
	var backend := _backend()

	var pending := backend.find_by_id(&"rocks", &"missing")
	assert_object(pending).is_not_null()
	assert_bool(pending.is_settled).override_failure_message(
		"an awaiting backend answered an already-settled promise, so its "
		+ "await either did not happen or crossed the boundary",
	).is_false()

	await NetwDatabase.settled_value(pending, { })
	assert_bool(pending.is_settled).is_true()
