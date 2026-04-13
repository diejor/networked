## Integration tests for SaveComponent + SaveSynchronizer with real multiplayer.
class_name TestSaveFlow
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SAVE_SCENE := preload("res://tests/helpers/TestLevelSave.tscn")

## Node path from level root to the ClientComponent spawner.
const SPAWNER_PATH := "TestPlayerWithSave/ClientComponent"

var harness: NetworkTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetworkedDatabase


func before_test() -> void:
	test_dir = create_temp_dir("save_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetworkedDatabase.new())
	db.backend = backend

	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)

	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	server_mgr.add_spawnable_scene(TEST_LEVEL_SAVE_SCENE.resource_path)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame


## Helper: joins a player via the real RPC chain, which triggers
## _on_player_joined → save_component.spawn(owner) automatically.
func _spawn_save_player() -> Node2D:
	var player := await harness.join_player(
		client0, TEST_LEVEL_SAVE_SCENE.resource_path, SPAWNER_PATH) as Node2D

	# Inject our unique database and backend configuration.
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.database = db
	save_comp.table_name = &"players"

	return player


# ---------------------------------------------------------------------------
# SaveSynchronizer.setup() — virtualization
# ---------------------------------------------------------------------------

func test_setup_initializes_synchronizer() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	assert_that(save_comp.save_synchronizer._initialized).is_true()


func test_setup_virtualizes_position_property() -> void:
	var player := await _spawn_save_player()
	var save_sync: SaveSynchronizer = player.get_node("%SaveComponent/%SaveSynchronizer")
	assert_that(save_sync.has_state_property(&"position")).is_true()


func test_setup_populates_container_with_initial_values() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	assert_that(save_comp.save_container.has_value(&"position")).is_true()


# ---------------------------------------------------------------------------
# pull_from_scene() / push_to_scene()
# ---------------------------------------------------------------------------

func test_pull_from_scene_captures_live_position() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(50, 75)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()

	assert_that(save_comp.save_container.get_value(&"position")).is_equal(Vector2(50, 75))


func test_push_to_scene_restores_position() -> void:
	var player := await _spawn_save_player()
	var save_comp: SaveComponent = player.get_node("%SaveComponent")

	save_comp.save_container.set_value(&"position", Vector2(99, 99))
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
# save_state() / load_state() — database persistence
# ---------------------------------------------------------------------------

func test_save_state_to_database() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	var err: Error = save_comp.save_state()
	assert_that(err).is_equal(OK)

	# Verify record exists in backend.
	var entity_id := save_comp._get_entity_id()
	var raw: Dictionary = backend._find_by_id(&"players", entity_id)
	assert_that(raw.get(&"position")).is_equal(Vector2(10, 20))


func test_load_state_restores_from_database() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(10, 20)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	save_comp.pull_from_scene()
	save_comp.save_state()

	player.position = Vector2.ZERO
	save_comp.save_container.set_value(&"position", Vector2.ZERO)

	var err: Error = save_comp.load_state()
	assert_that(err).is_equal(OK)
	assert_that(player.position).is_equal(Vector2(10, 20))


# ---------------------------------------------------------------------------
# serialize_scene() / deserialize_scene() — network byte transfer
# ---------------------------------------------------------------------------

func test_serialize_deserialize_round_trip() -> void:
	var player := await _spawn_save_player()
	player.position = Vector2(55, 66)

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	var bytes := save_comp.serialize_scene()
	assert_that(bytes.size() > 0).is_true()

	player.position = Vector2.ZERO
	save_comp.deserialize_scene(bytes)

	assert_that(player.position).is_equal(Vector2(55, 66))
