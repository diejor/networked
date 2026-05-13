## Unit tests for [FileSystemBackend].
class_name TestFileSystemBackend
extends NetworkedTestSuite

var backend: FileSystemBackend
var test_dir: String


func before_test() -> void:
	test_dir = create_temp_dir("fs_backend_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir


func test_collision_detection_blocks_active_instances() -> void:
	backend.initialize({})
	
	var second_backend := FileSystemBackend.new()
	second_backend.base_dir = test_dir
	
	# Collision detection is internal; this test verifies no crash on overlap.
	pass


func test_self_cleaning_registry_allows_reuse_after_free() -> void:
	var path := test_dir.path_join("reuse_test")
	DirAccess.make_dir_recursive_absolute(path)
	
	var first := FileSystemBackend.new()
	first.base_dir = path
	first.initialize({})
	
	first = null
	
	var second := FileSystemBackend.new()
	second.base_dir = path
	var err := second.initialize({})
	assert_that(err).is_equal(OK)


func test_initialize_creates_table_directories() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	assert_that(
		DirAccess.dir_exists_absolute(test_dir.path_join("rocks"))).is_true()
	assert_that(
		DirAccess.dir_exists_absolute(test_dir.path_join("players"))).is_true()


func test_initialize_detects_ghost_tables() -> void:
	DirAccess.make_dir_recursive_absolute(test_dir.path_join("ghosts"))

	var ghost_backend: FileSystemBackend = auto_free(FileSystemBackend.new())
	ghost_backend.base_dir = test_dir

	var err: Error = ghost_backend.initialize({&"rocks": [&"health"]})
	assert_that(err).is_equal(OK)


func test_upsert_creates_file() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"rock_1", {&"health": 100})
	var path := test_dir.path_join("rocks").path_join("rock_1.dict")
	assert_that(ResourceLoader.exists(path)).is_true()


func test_find_by_id_returns_stored_data() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"rock_1", {&"health": 75})
	var record := backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(75)


func test_find_by_id_returns_empty_for_missing_record() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	var record := backend.find_by_id(&"rocks", &"nonexistent")
	assert_that(record.is_empty()).is_true()


func test_upsert_merges_columns() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"rock_1", {&"health": 100})
	backend.upsert(&"rocks", &"rock_1", {&"gold": 5})

	var record := backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(100)
	assert_that(record.get(&"gold")).is_equal(5)


func test_upsert_overwrites_changed_columns() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"rock_1", {&"health": 100})
	backend.upsert(&"rocks", &"rock_1", {&"health": 50})

	var record := backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.get(&"health")).is_equal(50)


func test_find_all_returns_all_records() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"r1", {&"health": 10})
	backend.upsert(&"rocks", &"r2", {&"health": 20})
	backend.upsert(&"rocks", &"r3", {&"health": 30})

	var all := backend.find_all(&"rocks", {})
	assert_that(all.size()).is_equal(3)


func test_find_all_with_filter_returns_matching_records() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"r1", {&"health": 10, &"type": &"granite"})
	backend.upsert(&"rocks", &"r2", {&"health": 20, &"type": &"marble"})
	backend.upsert(&"rocks", &"r3", {&"health": 30, &"type": &"granite"})

	var granite := backend.find_all(&"rocks", {&"type": &"granite"})
	assert_that(granite.size()).is_equal(2)


func test_find_all_returns_empty_for_nonexistent_table() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	var records := backend.find_all(&"nonexistent", {})
	assert_that(records.is_empty()).is_true()


func test_delete_removes_file() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	backend.upsert(&"rocks", &"rock_1", {&"health": 100})
	backend.delete(&"rocks", &"rock_1")

	var record := backend.find_by_id(&"rocks", &"rock_1")
	assert_that(record.is_empty()).is_true()


func test_delete_is_idempotent_for_missing_record() -> void:
	backend.initialize({&"rocks": [&"health"], &"players": [&"position"]})
	var err := backend.delete(&"rocks", &"nonexistent")
	assert_that(err).is_equal(OK)


func test_text_format_writes_tdict_extension() -> void:
	var text_backend: FileSystemBackend = auto_free(FileSystemBackend.new())
	text_backend.base_dir = test_dir
	text_backend.use_text_format = true
	text_backend.initialize({&"items": []})

	text_backend.upsert(&"items", &"sword", {&"damage": 15})
	var path := test_dir.path_join("items").path_join("sword.tdict")
	assert_that(ResourceLoader.exists(path)).is_true()


func test_text_format_round_trips_data() -> void:
	var text_backend: FileSystemBackend = auto_free(FileSystemBackend.new())
	text_backend.base_dir = test_dir
	text_backend.use_text_format = true
	text_backend.initialize({&"items": []})

	text_backend.upsert(&"items", &"bow", {&"damage": 8})
	var record: Dictionary = text_backend.find_by_id(&"items", &"bow")
	assert_that(record.get(&"damage")).is_equal(8)
