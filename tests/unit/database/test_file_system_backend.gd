## Unit tests for [FileSystemBackend].
class_name TestFileSystemBackend
extends NetwTestSuite

func test_initialization_lifecycle_and_registry() -> void:
	var fs_dir := create_temp_dir("fs_backend_init")

	# 1. Initialize creates directories
	var backend_1 := FileSystemBackend.new()
	backend_1.base_dir = fs_dir
	var err := backend_1.initialize({ &"rocks": [&"health"] })
	assert_that(err).is_equal(OK)
	assert_that(
		DirAccess.dir_exists_absolute(fs_dir.path_join("rocks")),
	).is_true()

	# Free backend_1 to clear registry lock
	backend_1 = null

	# 2. Initialize detects ghost tables (must warning/return OK)
	DirAccess.make_dir_recursive_absolute(fs_dir.path_join("ghosts"))
	var backend_ghost := FileSystemBackend.new()
	backend_ghost.base_dir = fs_dir
	var ghost_err := backend_ghost.initialize({ &"rocks": [&"health"] })
	assert_that(ghost_err).is_equal(OK)

	# Free backend_ghost to clear registry lock
	backend_ghost = null

	# 3. Self-cleaning registry allows reuse after freeing
	var backend_2 := FileSystemBackend.new()
	backend_2.base_dir = fs_dir
	var err_reuse := backend_2.initialize({ &"rocks": [&"health"] })
	assert_that(err_reuse).is_equal(OK)


@warning_ignore("unused_parameter")
func test_crud_flow_and_querying(
		use_text_format: bool,
		test_parameters := [
			[false],
			[true],
		],
) -> void:
	var fs_dir := create_temp_dir("fs_backend_crud_" + str(use_text_format))
	var fs_backend: FileSystemBackend = auto_free(FileSystemBackend.new())
	fs_backend.base_dir = fs_dir
	fs_backend.use_text_format = use_text_format

	# 1. Initialize
	var schema := { &"rocks": [&"health", &"type"], &"players": [&"position"] }
	var err := fs_backend.initialize(schema)
	assert_that(err).is_equal(OK)

	# Check table directories and file extension
	var ext := ".tdict" if use_text_format else ".dict"
	assert_that(
		DirAccess.dir_exists_absolute(fs_dir.path_join("rocks")),
	).is_true()

	# 2. Upsert
	fs_backend.upsert(&"rocks", &"r1", { &"health": 100, &"type": &"granite" })
	fs_backend.upsert(&"rocks", &"r2", { &"health": 50, &"type": &"marble" })

	# Verify file exists on disk
	var file_path := fs_dir.path_join("rocks").path_join("r1" + ext)
	assert_that(ResourceLoader.exists(file_path)).is_true()

	# 3. Find by ID
	var r1 := fs_backend.find_by_id(&"rocks", &"r1")
	assert_that(r1.get(&"health")).is_equal(100)
	assert_that(r1.get(&"type")).is_equal(StringName("granite"))

	# 4. Upsert (Merge & Overwrite)
	fs_backend.upsert(&"rocks", &"r1", { &"gold": 5 })
	fs_backend.upsert(&"rocks", &"r1", { &"health": 80 })

	var r1_updated := fs_backend.find_by_id(&"rocks", &"r1")
	assert_that(r1_updated.get(&"health")).is_equal(80)
	assert_that(r1_updated.get(&"gold")).is_equal(5)

	# 5. Find All (Query / Filter)
	var all_rocks := fs_backend.find_all(&"rocks", { })
	assert_that(all_rocks.size()).is_equal(2)

	var granite_rocks := fs_backend.find_all(&"rocks", { &"type": &"granite" })
	assert_that(granite_rocks.size()).is_equal(1)
	assert_that(granite_rocks[0].get(&"health")).is_equal(80)

	var nonexistent := fs_backend.find_all(&"nonexistent", { })
	assert_that(nonexistent.is_empty()).is_true()

	# 6. Delete (Idempotent)
	var del_err := fs_backend.delete(&"rocks", &"r1")
	assert_that(del_err).is_equal(OK)
	assert_that(fs_backend.find_by_id(&"rocks", &"r1").is_empty()).is_true()

	var del_missing_err := fs_backend.delete(&"rocks", &"nonexistent")
	assert_that(del_missing_err).is_equal(OK)
