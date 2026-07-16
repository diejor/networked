## Integration tests for persistence with real multiplayer.
##
## A spawned player's [member NetwEntity.persistence] engine flushes its persisted
## columns to the database and hydrates them back, reading and writing the live
## scene the whole time.
class_name TestSaveFlow
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayerWithSave"

var harness: NetwTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemDatabase
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	test_dir = create_temp_dir("save_flow_test")
	backend = auto_free(FileSystemDatabase.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	player_builder = PlayerBuilder.new("TestPlayerWithSave") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_save(db, &"players_save") \
			.with_save_property(&"position") \
			.with_player_sync(
				SyncConfigBuilder.new().property("..:position", true),
			)
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new("TestLevelSave") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)

	client0 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func _spawn_save_player() -> Node2D:
	var player := await harness.join_player(
		client0,
		level_builder.resource_path,
		SPAWNER_PATH,
	) as Node2D

	player.set_meta(
		NetwPersistenceInterface.PersistenceEngine.META_DATABASE, db,
	)
	await get_tree().process_frame
	return player


func _engine(player: Node) -> NetwPersistenceInterface.PersistenceEngine:
	return NetwEntity.of(player).persistence


func test_engine_present_and_tracks_position() -> void:
	var player := await _spawn_save_player()
	var engine := _engine(player)

	assert_that(engine).is_not_null()
	assert_that(engine.columns_empty()).is_false()
	assert_that(engine.database()).is_same(db)


func test_gather_and_apply_round_trip_scene_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(50, 75)

	var engine := _engine(player)
	assert_that(engine.gather().get(&"position")).is_equal(Vector2(50, 75))

	engine.apply({ &"position": Vector2(99, 99) })
	assert_that(player.position).is_equal(Vector2(99, 99))


func test_database_round_trip_restores_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var engine := _engine(player)
	var err: Error = await engine.flush()
	assert_that(err).is_equal(OK)

	var raw: Dictionary = backend._find_by_id(&"players_save", engine._record_id())
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))

	player.position = Vector2.ZERO
	await engine.hydrate()
	assert_that(player.position).is_equal(Vector2(10, 20))
