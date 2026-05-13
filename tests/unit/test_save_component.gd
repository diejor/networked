## Unit tests for [SaveComponent], DB persistence, and synchronization.
##
## All tests run without a [SceneTree], network, or spawner.
class_name TestSaveComponent
extends NetworkedTestSuite

var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_component_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend


func test_get_entity_id_uses_username_when_client_present() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	root.add_child(save_comp)
	save_comp.owner = root

	var client: SpawnerComponent = auto_free(SpawnerComponent.new())
	client.name = "SpawnerComponent"
	client.unique_name_in_owner = true
	root.add_child(client)
	client.owner = root
	client.entity_id = &"alice"

	assert_that(save_comp._get_entity_id()).is_equal(&"alice")


func test_get_entity_id_uses_node_name_without_client() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "MyPlayer"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	root.add_child(save_comp)
	save_comp.owner = root

	assert_that(save_comp._get_entity_id()).is_equal(&"MyPlayer")


func test_flush_and_hydrate_round_trip_via_database() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Alice"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	save_comp.bound_entity.set_value(&"health", 100)
	db._register_schema(&"players", [&"health"])

	var err: Error = save_comp._flush()
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend.find_by_id(&"players", &"Alice")
	assert_that(raw.get(&"health")).is_equal(100)

	save_comp.bound_entity.set_value(&"health", 0)
	save_comp.hydrate(raw)
	assert_that(save_comp.bound_entity.get_value(&"health")).is_equal(100)


func test_hydrate_empty_record_uses_scene_defaults() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Ghost"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"health"])

	var signal_fired := [false]
	save_comp.loaded.connect(func(): signal_fired[0] = true)
	save_comp.hydrate({})
	assert_that(signal_fired[0]).is_true()


func test_flush_uses_entity_to_dict() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Bob"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	save_comp.bound_entity.set_value(&"score", 999)
	save_comp.bound_entity.set_value(&"level", 5)
	db._register_schema(&"players", [&"score", &"level"])
	save_comp._flush()

	var raw: Dictionary = backend.find_by_id(&"players", &"Bob")
	assert_that(raw.get(&"score")).is_equal(999)
	assert_that(raw.get(&"level")).is_equal(5)


func test_bind_entity_via_table_repository() -> void:
	db._register_schema(&"players", [&"score"])
	await get_tree().process_frame

	db.transaction(func(tx: NetwDatabase.TransactionContext) -> void:
		tx.queue_upsert(&"players", &"carol", {&"score": 42})
	)

	var entity: Entity = db.table(&"players").fetch(&"carol")
	assert_that(entity).is_not_null()
	assert_that(entity.get_value(&"score")).is_equal(42)


func test_table_repository_put_persists_entity() -> void:
	db._register_schema(&"players", [&"score"])
	await get_tree().process_frame

	var entity: DictionaryEntity = DictionaryEntity.new()
	entity.set_value(&"score", 77)

	var err: Error = db.table(&"players").put(&"dave", entity)
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend.find_by_id(&"players", &"dave")
	assert_that(raw.get(&"score")).is_equal(77)


func test_push_to_scene_writes_tracked_property_to_node() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var entity: DictionaryEntity = auto_free(DictionaryEntity.new())
	entity.set_value(&"position", Vector2(10.0, 20.0))

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.bound_entity = entity
	save_comp.track(&"position", NodePath("..:position"))
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"position"])
	save_comp._instantiate_sync()

	var err: Error = save_comp.push_to_scene()
	assert_that(err).is_equal(OK)
	assert_that(root.position).is_equal(Vector2(10.0, 20.0))


func test_pull_from_scene_reads_node_into_entity() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"
	root.position = Vector2(5.0, 15.0)

	var entity: DictionaryEntity = auto_free(DictionaryEntity.new())
	entity.set_value(&"position", Vector2.ZERO)

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.bound_entity = entity
	save_comp.track(&"position", NodePath("..:position"))
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"position"])
	save_comp._instantiate_sync()

	save_comp.pull_from_scene()
	assert_that(entity.get_value(&"position")).is_equal(Vector2(5.0, 15.0))


func test_untracked_entity_keys_do_not_affect_scene() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var entity: DictionaryEntity = auto_free(DictionaryEntity.new())
	entity.set_value(&"position", Vector2(1.0, 2.0))
	entity.set_value(&"inventory_size", 10)

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.bound_entity = entity
	save_comp.track(&"position", NodePath("..:position"))
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"position", &"inventory_size"])
	save_comp._instantiate_sync()
	save_comp.push_to_scene()

	assert_that(root.position).is_equal(Vector2(1.0, 2.0))
	assert_that(entity.get_value(&"inventory_size")).is_equal(10)


func test_is_dirty_reflects_network_and_manual_writes() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	root.add_child(save_comp)
	save_comp.owner = root
	save_comp.track(&"health", NodePath("..:modulate"))

	assert_that(save_comp.is_dirty()).is_false()

	save_comp.set_value(&"health", 50)
	assert_that(save_comp.is_dirty()).is_true()


func test_get_value_returns_bound_entity_data() -> void:
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.bound_entity.set_value(&"points", 100)

	assert_that(save_comp.get_value(&"points")).is_equal(100)
	assert_that(save_comp.get_value(&"missing", 0)).is_equal(0)


func test_fetch_reflects_record_existence() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Dave"
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"created"])

	assert_that(db.table(&"players").fetch(&"Dave")).is_null()

	db.transaction(func(tx: NetwDatabase.TransactionContext) -> void:
		tx.queue_upsert(&"players", &"Dave", {&"created": true})
	)
	await get_tree().process_frame

	assert_that(db.table(&"players").fetch(&"Dave")).is_not_null()


func test_get_virtual_properties_lists_all_keys() -> void:
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.track(&"a", NodePath("."))
	save_comp.track(&"b", NodePath("."))
	save_comp.finalize()

	var props := save_comp.get_virtual_properties()
	assert_that(props).contains_exactly_in_any_order([&"a", &"b"])
