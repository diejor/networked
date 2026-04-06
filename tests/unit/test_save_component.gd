## Tests SaveComponent logic that doesn't require multiplayer or scene tree entry.
##
## These tests build manual node trees (never added to the scene tree) to test
## path resolution, registration, and disk I/O without triggering _ready()
## which calls set_visibility_for() on the SaveSynchronizer.
class_name TestSaveComponent
extends GdUnitTestSuite

var save_dir: String


func before_test() -> void:
	save_dir = create_temp_dir("save_component_test")


# ---------------------------------------------------------------------------
# get_save_path()
# ---------------------------------------------------------------------------

func test_get_save_path_uses_username_when_client_present() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var save_comp := SaveComponent.new()
	save_comp.save_dir = save_dir
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
	save_comp.save_dir = save_dir
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

	var path := save_dir.path_join("round_trip_test.tres")
	var err := ResourceSaver.save(save, path)
	assert_that(err).is_equal(OK)

	var loaded := ResourceLoader.load(path, "DictionarySave", ResourceLoader.CACHE_MODE_REPLACE)
	assert_that(loaded is DictionarySave).is_true()
	assert_that(loaded.get_value(&"health")).is_equal(100)
	assert_that(loaded.get_value(&"pos")).is_equal(Vector2(10, 20))
