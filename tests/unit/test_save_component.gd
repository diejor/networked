## Tests SaveComponent logic that doesn't require multiplayer or scene tree entry.
class_name TestSaveComponent
extends NetworkedTestSuite

var test_dir: String
var backend: FileSystemBackend
var db: NetworkedDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_component_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetworkedDatabase.new())
	db.backend = backend


# ---------------------------------------------------------------------------
# Entity ID / Record ID
# ---------------------------------------------------------------------------

func test_get_entity_id_uses_username_when_client_present() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.save_container = DictionarySave.new()
	root.add_child(save_comp)
	save_comp.owner = root

	var client: ClientComponent = auto_free(ClientComponent.new())
	client.name = "ClientComponent"
	client.unique_name_in_owner = true
	client.username = "alice"
	root.add_child(client)
	client.owner = root

	assert_that(save_comp._get_entity_id()).is_equal(&"alice")


func test_get_entity_id_uses_node_name_without_client() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "MyPlayer"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.save_container = DictionarySave.new()
	root.add_child(save_comp)
	save_comp.owner = root

	assert_that(save_comp._get_entity_id()).is_equal(&"MyPlayer")


# ---------------------------------------------------------------------------
# Database save/load
# ---------------------------------------------------------------------------

func test_save_and_load_round_trip_via_database() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Alice"

	var container: DictionarySave = auto_free(DictionarySave.new())
	container.set_value(&"health", 100)

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.save_container = container
	root.add_child(save_comp)
	save_comp.owner = root

	# Must register schema before saving.
	db.register_schema(&"players", [&"health"])

	var err: Error = save_comp.save_state()
	assert_that(err).is_equal(OK)

	# Verify record exists in backend.
	var raw: Dictionary = backend._find_by_id(&"players", &"Alice")
	assert_that(raw.get(&"health")).is_equal(100)

	# Clear container and reload.
	container.set_value(&"health", 0)
	var load_err: Error = save_comp.load_state()
	assert_that(load_err).is_equal(OK)
	assert_that(container.get_value(&"health")).is_equal(100)
