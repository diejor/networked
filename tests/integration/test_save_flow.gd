## Integration tests for SaveComponent with real multiplayer.
class_name TestSaveFlow
extends NetworkedTestSuite

const TEST_LEVEL_SAVE_SCENE := preload("res://tests/helpers/TestLevelSave.tscn")

## Node path from level root to the [SpawnerPlayerComponent] spawner.
const SPAWNER_PATH := "TestPlayerWithSave/SpawnerPlayerComponent"

var harness: NetworkTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	var server_mgr := harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SAVE_SCENE.resource_path)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


## Helper: joins a player via the real RPC chain, which triggers
## _on_player_joined → NetwSpawn.configure → save_comp.hydrate automatically.
func _spawn_save_player() -> Node2D:
	var player := await harness.join_player(
		client0, TEST_LEVEL_SAVE_SCENE.resource_path, SPAWNER_PATH) as Node2D

	# Inject our unique database and backend configuration.
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"
	# Re-run sync setup so the injected database gets the schema registered.
	save_comp._instantiate_sync()
	await get_tree().process_frame  # let deferred _initialize_backend run

	return player


# ---------------------------------------------------------------------------
# SaveComponent setup — config
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# pull_from_scene() / push_to_scene()
# ---------------------------------------------------------------------------

func test_pull_from_scene_captures_live_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(50, 75)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()

	assert_that(save_comp.bound_entity.get_value(&"position")).is_equal(Vector2(50, 75))


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


# ---------------------------------------------------------------------------
# _flush() / hydrate() — database persistence
# ---------------------------------------------------------------------------

func test_flush_state_to_database() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	var err: Error = save_comp._flush()
	assert_that(err).is_equal(OK)

	# Verify record exists in backend.
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


# ---------------------------------------------------------------------------
# _serialize_scene() / _deserialize_scene() — network byte transfer
# ---------------------------------------------------------------------------

func test_serialize_deserialize_round_trip() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(55, 66)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	var bytes := save_comp._serialize_scene()
	assert_that(bytes.size() > 0).is_true()

	player.position = Vector2.ZERO
	save_comp._deserialize_scene(bytes)

	assert_that(player.position).is_equal(Vector2(55, 66))
