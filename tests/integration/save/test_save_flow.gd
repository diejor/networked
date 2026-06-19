## Integration tests for [SaveComponent] with real multiplayer.
class_name TestSaveFlow
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayerWithSave/MultiplayerEntity"

var harness: NetwTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	test_dir = create_temp_dir("save_flow_test")
	backend = auto_free(FileSystemBackend.new())
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

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"
	await get_tree().process_frame

	return player


func test_setup_initializes_sync_and_tracks_position() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")

	assert_that(save_comp._initialized).is_true()
	assert_that(save_comp.has_virtual_property(&"position")).is_true()
	assert_that(save_comp.record.has_value(&"position")).is_true()


func test_pull_and_push_round_trip_scene_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(50, 75)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()

	assert_that(save_comp.record.get_value(&"position")).is_equal(
		Vector2(50, 75),
	)

	save_comp.record.set_value(&"position", Vector2(99, 99))
	save_comp.push_to_scene()
	assert_that(player.position).is_equal(Vector2(99, 99))

	player.position = Vector2(33, 44)
	save_comp.pull_from_scene()
	player.position = Vector2.ZERO
	save_comp.push_to_scene()
	assert_that(player.position).is_equal(Vector2(33, 44))


func test_database_and_serialized_round_trips_restore_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	var err: Error = save_comp._flush()
	assert_that(err).is_equal(OK)

	var entity_id := save_comp._get_entity_id()
	var raw: Dictionary = backend.find_by_id(&"players", entity_id)
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))

	player.position = Vector2.ZERO
	save_comp.record.set_value(&"position", Vector2.ZERO)
	save_comp.hydrate(raw)
	assert_that(player.position).is_equal(Vector2(10, 20))

	player.position = Vector2(55, 66)
	var bytes := save_comp._serialize_scene()
	assert_that(bytes.size() > 0).is_true()

	player.position = Vector2.ZERO
	save_comp._deserialize_scene(bytes)

	assert_that(player.position).is_equal(Vector2(55, 66))
