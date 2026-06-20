## Verifies the [NetwDatabaseBackend] abstract contract via a minimal stub.
class_name TestNetwBackend
extends NetwTestSuite

func test_initialize_returns_ok() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var err: Error = backend.initialize({ })
	assert_that(err).is_equal(OK)


func test_upsert_and_find_by_id_round_trip() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var err: Error = backend.upsert(&"rocks", &"rock_1", { &"health": 50 })
	assert_that(err).is_equal(OK)

	var record: Dictionary = backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(50)


func test_upsert_merges_columns() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	backend.upsert(&"rocks", &"rock_1", { &"health": 50 })
	backend.upsert(&"rocks", &"rock_1", { &"gold": 10 })

	var record: Dictionary = backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(50)
	assert_that(record.get(&"gold")).is_equal(10)


func test_find_by_id_returns_empty_for_missing_record() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	var record: Dictionary = backend.find_by_id(&"rocks", &"nonexistent")
	assert_that(record.is_empty()).is_true()


func test_find_all_returns_all_records_and_filters() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	backend.upsert(&"rocks", &"r1", { &"type": &"granite" })
	backend.upsert(&"rocks", &"r2", { &"type": &"marble" })
	backend.upsert(&"rocks", &"r3", { &"type": &"granite" })

	var all: Array[Dictionary] = backend.find_all(&"rocks", { })
	assert_that(all.size()).is_equal(3)

	var granite: Array[Dictionary] = backend.find_all(
		&"rocks",
		{ &"type": &"granite" },
	)
	assert_that(granite.size()).is_equal(2)


func test_delete_removes_record() -> void:
	var backend: TestMemoryBackend = auto_free(TestMemoryBackend.new())
	backend.upsert(&"rocks", &"rock_1", { &"health": 50 })
	backend.delete(&"rocks", &"rock_1")

	var record: Dictionary = backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.is_empty()).is_true()
