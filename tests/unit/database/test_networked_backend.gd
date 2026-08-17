## Verifies the [NetwDatabaseBackend] abstract contract via a minimal stub.
##
## The two refusals below can only be provoked from GDScript: the dispatcher
## that ships them runs on the GDVIRTUAL path, which a native subclass
## overriding the same verb never enters, so no hosted case can construct the
## override that triggers them.
class_name TestNetwBackend
extends NetwTestSuite


# A backend whose override answers nothing at all, which is what a half-written
# one looks like from the caller's side.
class SilentBackend extends NetwDatabaseBackend:
	func _find_by_id(_table: StringName, _id: StringName) -> NetwPromise:
		return null


# A backend whose writes settle later, which is what one talking to a service
# over a socket does.
class DeferringBackend extends NetwDatabaseBackend:
	func _upsert(
			_table: StringName,
			_id: StringName,
			_data: Dictionary,
	) -> NetwPromise:
		return NetwPromise.new()

func test_initialize_returns_ok() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var err: Error = _now(backend.initialize({ }))
	assert_that(err).is_equal(OK)


func test_upsert_and_find_by_id_round_trip() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var err: Error = _now(backend.upsert(&"rocks", &"rock_1", { &"health": 50 }))
	assert_that(err).is_equal(OK)

	var record: Dictionary = _now(backend.find_by_id(&"rocks", &"rock_1"))
	assert_that(record.get(&"health")).is_equal(50)


func test_upsert_merges_columns() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	_now(backend.upsert(&"rocks", &"rock_1", { &"health": 50 }))
	_now(backend.upsert(&"rocks", &"rock_1", { &"gold": 10 }))

	var record: Dictionary = _now(backend.find_by_id(&"rocks", &"rock_1"))
	assert_that(record.get(&"health")).is_equal(50)
	assert_that(record.get(&"gold")).is_equal(10)


func test_find_by_id_returns_empty_for_missing_record() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var record: Dictionary = _now(backend.find_by_id(&"rocks", &"nonexistent"))
	assert_that(record.is_empty()).is_true()


func test_find_all_returns_all_records_and_filters() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	_now(backend.upsert(&"rocks", &"r1", { &"type": &"granite" }))
	_now(backend.upsert(&"rocks", &"r2", { &"type": &"marble" }))
	_now(backend.upsert(&"rocks", &"r3", { &"type": &"granite" }))

	var all: Array[Dictionary] = _now(backend.find_all(&"rocks", { }))
	assert_that(all.size()).is_equal(3)

	var granite: Array[Dictionary] = _now(backend.find_all(
		&"rocks",
		{ &"type": &"granite" },
	))
	assert_that(granite.size()).is_equal(2)


func test_delete_removes_record() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	_now(backend.upsert(&"rocks", &"rock_1", { &"health": 50 }))
	_now(backend.erase(&"rocks", &"rock_1"))

	var record: Dictionary = _now(backend.find_by_id(&"rocks", &"rock_1"))
	assert_that(record.is_empty()).is_true()


## Verify a backend whose override answers no promise is refused rather than
## handed back as a null the caller would await forever.
func test_a_backend_that_answers_no_promise_is_refused() -> void:
	var backend: SilentBackend = auto_free(SilentBackend.new())

	var answer := backend.find_by_id(&"rocks", &"r1")

	assert_object(answer).is_not_null()
	assert_bool(answer.is_failed).is_true()
	assert_int(answer.code).is_equal(ERR_INVALID_DATA)


## Verify the default commit refuses a backend whose writes settle later,
## rather than reporting a batch it never saw finish.
##
## Sequencing writes that settle later is the backend's own job, and the
## fallback loop cannot do it: it would have to wait, and waiting is the one
## thing the boundary keeps out of C++.
func test_a_commit_over_deferred_writes_is_refused() -> void:
	var backend: DeferringBackend = auto_free(DeferringBackend.new())

	var answer := backend.commit(
		[{ table = &"rocks", id = &"r1", data = { &"health": 1 } }],
	)

	assert_object(answer).is_not_null()
	assert_bool(answer.is_failed).is_true()
	assert_int(answer.code).is_equal(ERR_UNAVAILABLE)


# What a synchronous backend answers: a promise that has ALREADY settled, so a
# caller reads its value without waiting. A backend that answered an unsettled
# promise here would be an asynchronous one wearing this contract.
func _now(promise: NetwPromise) -> Variant:
	assert_bool(promise != null).is_true()
	assert_bool(promise.is_settled).is_true()
	return promise.result
