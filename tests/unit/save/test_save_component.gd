## Unit tests for [NetwPersistenceEngine].
##
## The scene is the record: an engine gathers persisted columns from live nodes,
## flushes them to a [NetwDatabase], and applies fetched rows back. All tests run
## without a network or spawner.
class_name TestSaveComponent
extends NetwTestSuite

var test_dir: String
var backend: FileSystemDatabase
var db: NetwDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_component_test")
	backend = auto_free(FileSystemDatabase.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend


# Builds an engine over [param root] persisting [param columns] to
# [param table]. The root declares its columns through metadata, the scriptless
# path.
func _engine_for(
		root: Node,
		table: StringName,
		columns: Array[StringName],
) -> NetwPersistenceEngine:
	var meta: Array = []
	for c in columns:
		meta.append({ "property": c })
	root.set_meta(
		NetwPersistenceEngine.META_COLUMNS,
		meta,
	)
	var entity := NetwEntity.ensure(root)
	var config := NetwScriptModel.PersistenceConfig.new()
	config.db = db
	config.table_name = table
	return NetwPersistenceEngine.new(entity, config)


func test_record_id_prefers_entity_id_then_node_name() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "MyPlayer"
	var engine := _engine_for(root, &"players", [&"position"])
	assert_that(engine._record_id()).is_equal(&"MyPlayer")

	NetwEntity.of(root).entity_id = &"valeria"
	assert_that(engine._record_id()).is_equal(&"valeria")


func test_flush_and_hydrate_round_trip_via_database() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria"
	var engine := _engine_for(root, &"players", [&"position"])

	root.position = Vector2(10, 20)
	var err: Error = await engine.flush()
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend.find_by_id(&"players", &"valeria")
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))

	root.position = Vector2.ZERO
	await engine.hydrate()
	assert_that(root.position).is_equal(Vector2(10, 20))


func test_hydrate_empty_record_emits_hydrated() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Ghost"
	var engine := _engine_for(root, &"players", [&"position"])

	var signal_fired := [false]
	engine.hydrated.connect(func(): signal_fired[0] = true)
	await engine.hydrate()
	assert_that(signal_fired[0]).is_true()


func test_gather_and_apply_reflect_live_scene() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"
	var engine := _engine_for(root, &"players", [&"position"])

	root.position = Vector2(5, 15)
	assert_that(engine.gather().get(&"position")).is_equal(Vector2(5, 15))

	engine.apply({ &"position": Vector2(1, 2) })
	assert_that(root.position).is_equal(Vector2(1, 2))


func test_flush_registers_schema() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Dave"
	var engine := _engine_for(root, &"players", [&"position"])

	await engine.flush()
	assert_that(db.get_registered_columns(&"players")).contains(&"position")


func test_is_dirty_tracks_scene_change() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"
	var engine := _engine_for(root, &"players", [&"position"])

	root.position = Vector2(3, 4)
	await engine.flush()
	assert_that(engine.is_dirty()).is_false()

	root.position = Vector2(9, 9)
	assert_that(engine.is_dirty()).is_true()


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


## Verify the snapshot loop is handed the write rather than issuing it, which
## is what lets one tick group every due engine into one transaction per
## database.
func test_a_due_snapshot_returns_the_write_instead_of_issuing_it() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria"
	var engine := _engine_for(root, &"players", [&"position"])
	root.position = Vector2(3, 4)

	var due := engine.snapshot_tick(10.0)

	assert_that(due[&"db"]).is_same(db)
	assert_that(due[&"table"]).is_equal(&"players")
	assert_that(due[&"id"]).is_equal(&"valeria")
	assert_that(due[&"values"]).is_equal({ &"position": Vector2(3, 4) })
	# Nothing was written: the loop owns the transaction.
	assert_bool(engine.is_dirty()).is_true()

	engine.commit_snapshot(due[&"values"])

	assert_bool(engine.is_dirty()).is_false()
	assert_that(engine.snapshot_tick(10.0)).is_equal({ })


## Verify every engine due in one tick shares one transaction per database, so
## two hundred entities cost one commit rather than two hundred.
func test_one_tick_is_one_transaction_per_database() -> void:
	var mt := MultiplayerTree.new()
	mt.name = "SaveTree"
	add_child(mt)
	auto_free(mt)

	var commits: Array[Array] = []
	db.transaction_committed.connect(
		func(tables: int, records: int) -> void:
			commits.append([tables, records]),
	)

	for name in [&"ana", &"jose"]:
		var root := Node2D.new()
		root.name = name
		root.set_meta(NetwPersistenceEngine.META_DATABASE, db)
		root.set_meta(NetwPersistenceEngine.META_TABLE, &"players")
		root.set_meta(
			NetwPersistenceEngine.META_COLUMNS,
			[{ "property": &"position" }],
		)
		mt.add_child(root)
		auto_free(root)
		var entity := NetwEntity.ensure(root)
		entity.entity_id = name
		mt.api._persistence.engine_for(entity)
		root.position = Vector2(1, 2)

	mt.api.persist_tick(10.0)
	await NetwTestSuite.drain_frames(get_tree(), 3)

	assert_int(commits.size()).is_equal(1)
	assert_int(commits[0][1]).is_equal(2)
