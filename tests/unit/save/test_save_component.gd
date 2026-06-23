## Unit tests for [SaveComponent], DB persistence, and synchronization.
##
## All tests run without a [SceneTree], network, or spawner.
class_name TestSaveComponent
extends NetwTestSuite

var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_component_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend


func test_get_entity_id_prefers_client_identity_then_node_name() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	root.add_child(save_comp)
	save_comp.owner = root

	var client: MultiplayerEntity = auto_free(MultiplayerEntity.new())
	client.name = "MultiplayerEntity"
	client.unique_name_in_owner = true
	root.add_child(client)
	client.owner = root
	client.entity_id = &"valeria"

	assert_that(save_comp._get_entity_id()).is_equal(&"valeria")

	var fallback_root: Node2D = auto_free(Node2D.new())
	fallback_root.name = "MyPlayer"

	var fallback_save: SaveComponent = auto_free(SaveComponent.new())
	fallback_root.add_child(fallback_save)
	fallback_save.owner = fallback_root

	assert_that(fallback_save._get_entity_id()).is_equal(&"MyPlayer")


func test_flush_and_hydrate_round_trip_via_database() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	save_comp.record.set_value(&"health", 100)
	db._register_schema(&"players", [&"health"])

	var err: Error = await save_comp._flush()
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend.find_by_id(&"players", &"valeria")
	assert_that(raw.get(&"health")).is_equal(100)

	save_comp.record.set_value(&"health", 0)
	save_comp.hydrate(raw)
	assert_that(save_comp.record.get_value(&"health")).is_equal(100)


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
	save_comp.hydrate({ })
	assert_that(signal_fired[0]).is_true()


func test_hydrate_from_db_registers_schema_before_read() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Ghost"
	root.position = Vector2(3, 4)

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.root_path = NodePath("..")
	save_comp.register_property(&"position", NodePath(".:position"))
	root.add_child(save_comp)
	save_comp.owner = root

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			save_comp.hydrate_from_db()
	).is_success()

	assert_that(db.get_registered_columns(&"players")).contains(&"position")


func test_flush_persists_all_entity_values() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "jose"

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	save_comp.record.set_value(&"score", 999)
	save_comp.record.set_value(&"level", 5)
	db._register_schema(&"players", [&"score", &"level"])
	await save_comp._flush()

	var raw: Dictionary = backend.find_by_id(&"players", &"jose")
	assert_that(raw.get(&"score")).is_equal(999)
	assert_that(raw.get(&"level")).is_equal(5)


func test_table_repository_fetch_and_put_round_trip_entities() -> void:
	db._register_schema(&"players", [&"score"])
	await get_tree().process_frame

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"carol", { &"score": 42 })
	)

	var entity: NetwRecord = await db.table(&"players").fetch(&"carol")
	assert_that(entity).is_not_null()
	assert_that(entity.get_value(&"score")).is_equal(42)

	var dave: DictionaryRecord = DictionaryRecord.new()
	dave.set_value(&"score", 77)

	var err: Error = await db.table(&"players").put(&"dave", dave)
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend.find_by_id(&"players", &"dave")
	assert_that(raw.get(&"score")).is_equal(77)


func test_tracked_scene_property_push_pull_and_ignores_untracked_keys() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var entity: DictionaryRecord = auto_free(DictionaryRecord.new())
	entity.set_value(&"position", Vector2(10.0, 20.0))

	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp.record = entity
	save_comp.root_path = NodePath("..")
	save_comp.register_property(&"position", NodePath(".:position"))
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"position", &"inventory_size"])
	save_comp._instantiate_sync()

	var err: Error = save_comp.push_to_scene()
	assert_that(err).is_equal(OK)
	assert_that(root.position).is_equal(Vector2(10.0, 20.0))

	root.position = Vector2(5.0, 15.0)
	save_comp.pull_from_scene()
	assert_that(entity.get_value(&"position")).is_equal(Vector2(5.0, 15.0))

	entity.set_value(&"position", Vector2(1.0, 2.0))
	entity.set_value(&"inventory_size", 10)
	save_comp.push_to_scene()

	assert_that(root.position).is_equal(Vector2(1.0, 2.0))
	assert_that(entity.get_value(&"inventory_size")).is_equal(10)


func test_value_helpers_report_dirty_state_and_virtual_properties() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.root_path = NodePath("..")
	root.add_child(save_comp)
	save_comp.owner = root
	save_comp.register_property(&"health", NodePath(".:modulate"))

	assert_that(save_comp.is_dirty()).is_false()

	save_comp.set_value(&"health", 50)
	assert_that(save_comp.is_dirty()).is_true()

	save_comp.record.set_value(&"points", 100)
	save_comp.register_property(&"a", NodePath(".:position"))
	save_comp.register_property(&"b", NodePath(".:position"))
	save_comp.finalize()
	assert_that(save_comp.get_value(&"points")).is_equal(100)
	assert_that(save_comp.get_value(&"missing", 0)).is_equal(0)
	assert_that(save_comp.get_virtual_properties()) \
			.contains_exactly_in_any_order([&"health", &"a", &"b"])


func test_fetch_reflects_record_existence() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Dave"
	var save_comp: SaveComponent = auto_free(SaveComponent.new())
	save_comp.database = db
	save_comp.table_name = &"players"
	root.add_child(save_comp)
	save_comp.owner = root

	db._register_schema(&"players", [&"created"])

	assert_that(await db.table(&"players").fetch(&"Dave")).is_null()

	await db.transaction(
		func(tx: NetwDatabase.TransactionContext) -> void:
			tx.queue_upsert(&"players", &"Dave", { &"created": true })
	)
	await get_tree().process_frame

	assert_that(await db.table(&"players").fetch(&"Dave")).is_not_null()
