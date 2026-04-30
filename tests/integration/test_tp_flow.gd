## Tests TPComponent teleportation logic with real multiplayer peers and the
## full request_join_player RPC join chain.
class_name TestTPFlow
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE = preload("uid://d3ag2052swfwd")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

## Node path from level root to the [SpawnerComponent] spawner.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetworkTestHarness
var client0: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetworkedDatabase


func before_test() -> void:
	test_dir = create_temp_dir("tp_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetworkedDatabase.new())
	db.backend = backend

	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)

	var server_mgr := harness._get_lobby_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	server_mgr.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


## Helper: joins a player via the real RPC chain and overrides its database.
func _spawn_tp_player(scene_path: String) -> Node2D:
	var player := await harness.join_player(
		client0, scene_path, SPAWNER_PATH) as Node2D

	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	if save_comp:
		save_comp.database = db
		save_comp.table_name = &"players"

	return player


func test_two_lobbies_spawned() -> void:
	var server_mgr := harness._get_lobby_manager(harness.get_server())
	assert_that(server_mgr.active_lobbies.size()).is_equal(2)


func test_tp_spawn_places_in_correct_lobby() -> void:
	var player := await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)

	var server_mgr := harness._get_lobby_manager(harness.get_server())
	var lobby: Lobby = server_mgr.active_lobbies.get(&"TestLevel")
	assert_that(player.get_parent()).is_equal(lobby.level)


func test_reparent_moves_player_between_lobbies() -> void:
	var server_player := await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)
	var client_player := await harness.wait_for_client_player_spawn(client0, &"TestLevel") as Node2D

	# Override client database as well
	var client_save: SaveComponent = client_player.get_node("%SaveComponent")
	if client_save:
		client_save.database = db
		client_save.table_name = &"players"

	var server_mgr := harness._get_lobby_manager(harness.get_server())
	var lobby2: Lobby = server_mgr.active_lobbies.get(&"TestLevel2")

	var tp_target := SceneNodePath.new()
	tp_target.scene_path = TEST_LEVEL_2_SCENE.resource_path
	tp_target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await timeout_await(client_tp.teleport(tp_target).completed)

	assert_that(server_player.get_parent()).is_equal(lobby2.level)


func test_teleported_snaps_to_marker() -> void:
	await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)
	var client_player := await harness.wait_for_client_player_spawn(client0, &"TestLevel") as Node2D

	# Override client database as well
	var client_save: SaveComponent = client_player.get_node("%SaveComponent")
	if client_save:
		client_save.database = db
		client_save.table_name = &"players"

	var tp_target := SceneNodePath.new()
	tp_target.scene_path = TEST_LEVEL_2_SCENE.resource_path
	tp_target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await timeout_await(client_tp.teleport(tp_target).completed)

	var client_player2 := await harness.wait_for_client_player_spawn(client0, &"TestLevel2") as Node2D
	assert_that(client_player2.global_position).is_equal(Vector2(100, 100))
