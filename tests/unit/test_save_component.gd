## Tests SaveComponent logic that doesn't require multiplayer or scene tree entry.
##
## These tests build manual node trees (never added to the scene tree) to test
## path resolution, registration, and disk I/O without triggering _ready()
## which calls set_visibility_for() on the SaveSynchronizer.
class_name TestSaveComponent
extends GdUnitTestSuite

const SAVE_DIR := "res://tests/tmp_saves"


func before_test() -> void:
	if not DirAccess.dir_exists_absolute(SAVE_DIR):
		DirAccess.make_dir_recursive_absolute(SAVE_DIR)


func after_test() -> void:
	_clean_save_dir()
	SaveComponent.registered_components.clear()


func _clean_save_dir() -> void:
	var dir := DirAccess.open(SAVE_DIR)
	if dir:
		dir.list_dir_begin()
		var file := dir.get_next()
		while not file.is_empty():
			if not dir.current_is_dir():
				dir.remove(file)
			file = dir.get_next()
		dir.list_dir_end()


# ---------------------------------------------------------------------------
# get_save_path()
# ---------------------------------------------------------------------------

func test_get_save_path_uses_username_when_client_present() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var save_comp := SaveComponent.new()
	save_comp.save_dir = SAVE_DIR
	save_comp.save_extension = ".tres"
	save_comp.save_container = DictionarySave.new()
	root.add_child(save_comp)
	save_comp.owner = root

	var client := ClientComponent.new()
	client.name = "ClientComponent"
	client.unique_name_in_owner = true
	client.username = "alice"
	root.add_child(client)
	client.owner = root

	var path := save_comp.get_save_path()
	assert_that(path.ends_with("alice.tres")).is_true()


func test_get_save_path_uses_node_name_without_client() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "MyPlayer"

	var save_comp := SaveComponent.new()
	save_comp.save_dir = SAVE_DIR
	save_comp.save_extension = ".tres"
	save_comp.save_container = DictionarySave.new()
	root.add_child(save_comp)
	save_comp.owner = root

	var path := save_comp.get_save_path()
	assert_that(path.ends_with("MyPlayer.tres")).is_true()


# ---------------------------------------------------------------------------
# Disk save/load
# ---------------------------------------------------------------------------

func test_save_and_load_round_trip_to_disk() -> void:
	var save := DictionarySave.new()
	save.set_value(&"health", 100)
	save.set_value(&"pos", Vector2(10, 20))

	var path := SAVE_DIR.path_join("round_trip_test.tres")
	var err := ResourceSaver.save(save, path)
	assert_that(err).is_equal(OK)

	var loaded := ResourceLoader.load(path, "DictionarySave", ResourceLoader.CACHE_MODE_REPLACE)
	assert_that(loaded is DictionarySave).is_true()
	assert_that(loaded.get_value(&"health")).is_equal(100)
	assert_that(loaded.get_value(&"pos")).is_equal(Vector2(10, 20))


# ---------------------------------------------------------------------------
# Static registration
# ---------------------------------------------------------------------------

func test_register_adds_to_static_list() -> void:
	var comp: SaveComponent = auto_free(SaveComponent.new())
	comp.save_container = DictionarySave.new()
	SaveComponent.register(comp)
	assert_that(SaveComponent.registered_components.has(comp)).is_true()


func test_register_is_idempotent() -> void:
	var comp: SaveComponent = auto_free(SaveComponent.new())
	comp.save_container = DictionarySave.new()
	SaveComponent.register(comp)
	SaveComponent.register(comp)
	var count := SaveComponent.registered_components.count(comp)
	assert_that(count).is_equal(1)


func test_unregister_removes_from_list() -> void:
	var comp: SaveComponent = auto_free(SaveComponent.new())
	comp.save_container = DictionarySave.new()
	SaveComponent.register(comp)
	SaveComponent.unregister(comp)
	assert_that(SaveComponent.registered_components.has(comp)).is_false()
