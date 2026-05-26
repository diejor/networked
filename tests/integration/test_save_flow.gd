## Integration tests for [SaveComponent] with real multiplayer.
class_name TestSaveFlow
extends NetwTestSuite

const PlayerBuilder := preload(
	"res://addons/networked_test/builders/player_builder.gd"
)
const LevelBuilder := preload(
	"res://addons/networked_test/builders/level_builder.gd"
)
const SPAWNER_PATH := "TestPlayerWithSave/SpawnerComponent"

var harness: NetwTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase
var player_packed: PackedScene
var level_packed: PackedScene


func before_test() -> void:
	test_dir = create_temp_dir("save_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	var player_builder: PlayerBuilder = PlayerBuilder.new("TestPlayerWithSave")
	var _r1: PlayerBuilder = player_builder.with_spawner()
	var _r2: PlayerBuilder = player_builder.with_save(db, &"players_save")
	player_packed = player_builder.pack()

	var template_instance: Node = player_packed.instantiate()
	var level_builder: LevelBuilder = LevelBuilder.new("TestLevelSave")
	var _r3: LevelBuilder = level_builder.with_multiplayer_spawner(
		"..", [player_packed]
	)
	var _r4: LevelBuilder = level_builder.with_child(template_instance)
	level_packed = level_builder.pack()
	template_instance.free()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_packed)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	if is_instance_valid(harness):
		await harness.teardown()


func _spawn_save_player() -> Node2D:
	var player := await harness.join_player(
		client0, level_packed.resource_path, SPAWNER_PATH) as Node2D

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"
	save_comp._instantiate_sync()
	await get_tree().process_frame

	return player


func test_setup_initializes_synchronizer() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	assert_that(save_comp._initialized).is_true()


func test_setup_tracks_position_property() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	assert_that(save_comp.has_virtual_property(&"position")).is_true()


func test_setup_populates_entity_with_initial_values() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	assert_that(save_comp.bound_entity.has_value(&"position")).is_true()


func test_pull_from_scene_captures_live_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(50, 75)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()

	assert_that(save_comp.bound_entity.get_value(&"position")).is_equal(
		Vector2(50, 75))


func test_push_to_scene_restores_position() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")

	save_comp.bound_entity.set_value(&"position", Vector2(99, 99))
	save_comp.push_to_scene()

	assert_that(player.position).is_equal(Vector2(99, 99))


func test_pull_then_push_round_trips() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(33, 44)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()

	player.position = Vector2.ZERO
	save_comp.push_to_scene()

	assert_that(player.position).is_equal(Vector2(33, 44))


func test_flush_state_to_database() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	var err: Error = save_comp._flush()
	assert_that(err).is_equal(OK)

	var entity_id := save_comp._get_entity_id()
	var raw: Dictionary = backend.find_by_id(&"players", entity_id)
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))


func test_hydrate_restores_from_database() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	save_comp._flush()

	player.position = Vector2.ZERO
	save_comp.bound_entity.set_value(&"position", Vector2.ZERO)

	var entity_id := save_comp._get_entity_id()
	var raw: Dictionary = backend.find_by_id(&"players", entity_id)
	save_comp.hydrate(raw)
	assert_that(player.position).is_equal(Vector2(10, 20))


func test_serialize_deserialize_round_trip() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(55, 66)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	var bytes := save_comp._serialize_scene()
	assert_that(bytes.size() > 0).is_true()

	player.position = Vector2.ZERO
	save_comp._deserialize_scene(bytes)

	assert_that(player.position).is_equal(Vector2(55, 66))
