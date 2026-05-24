## Tests [TPComponent] teleportation with real multiplayer peers.
##
## Covers the full [method MultiplayerTree.request_join_player] RPC join chain
## and cross-scene teleportation.
class_name TestTPFlow
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

## Node path from level root to the [SpawnerComponent] spawner.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetworkTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var test_dir: String
var backend: FileSystemBackend
var db: NetwDatabase


func before_test() -> void:
	test_dir = create_temp_dir("tp_flow_test")
	backend = auto_free(FileSystemBackend.new())
	backend.base_dir = test_dir
	db = auto_free(NetwDatabase.new())
	db.backend = backend

	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	var server_mgr := harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	server_mgr.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)

	client0 = await harness.add_client()


func after_test() -> void:
	clean_temp_dir()
	await drain_frames(get_tree(), 3)
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


## Joins a player via the real RPC chain and overrides its database.
func _spawn_tp_player(
	scene_path: String,
	client: MultiplayerTree = null,
) -> Node2D:
	if client == null:
		client = client0
	var player := await harness.join_player(
		client, scene_path, SPAWNER_PATH) as Node2D

	_set_player_database(player)
	return player


func _set_player_database(player: Node) -> void:
	var save_comp: SaveComponent = player.get_node("%SaveComponent")
	if save_comp:
		save_comp.database = db
		save_comp.table_name = &"players"


func _tp_target(scene_path: String, node_path: String) -> SceneNodePath:
	var target := SceneNodePath.new()
	target.scene_path = scene_path
	target.node_path = node_path
	return target


func _await_tp(promise: TPComponent.TeleportPromise) -> void:
	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while not promise.is_completed:
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			fail(
				"Timed out waiting for teleport completion after %.1f seconds."
					% DEFAULT_TIMEOUT)
			return


func test_two_scenes_spawned() -> void:
	var server_mgr := harness._get_scene_manager(harness.get_server())
	assert_that(server_mgr.active_scenes.size()).is_equal(2)


func test_tp_spawn_places_in_correct_scene() -> void:
	var player := await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)

	var server_mgr := harness._get_scene_manager(harness.get_server())
	var scene: MultiplayerScene = server_mgr.active_scenes.get(&"TestLevel")
	assert_that(player.get_parent()).is_equal(scene.level)


func test_reparent_moves_player_between_scenes() -> void:
	var server_player := await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)
	var client_player := await harness.wait_for_client_player_spawn(
		client0, &"TestLevel") as Node2D

	_set_player_database(client_player)

	var server_mgr := harness._get_scene_manager(harness.get_server())
	var scene2: MultiplayerScene = server_mgr.active_scenes.get(&"TestLevel2")

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(client_tp.teleport(
		_tp_target(TEST_LEVEL_2_SCENE.resource_path, "TPTarget")
	))

	assert_that(server_player.get_parent()).is_equal(scene2.level)


func test_teleported_snaps_to_marker() -> void:
	await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path)
	var client_player := await harness.wait_for_client_player_spawn(
		client0, &"TestLevel") as Node2D

	_set_player_database(client_player)

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await _await_tp(client_tp.teleport(
		_tp_target(TEST_LEVEL_2_SCENE.resource_path, "TPTarget")
	))

	var client_player2 := await harness.wait_for_client_player_spawn(
		client0, &"TestLevel2") as Node2D
	assert_that(client_player2.global_position).is_equal(Vector2(100, 100))


#func test_client_teleports_with_second_client_connected() -> void:
	#client1 = await harness.add_client()
	#await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path, client0)
	#await _spawn_tp_player(TEST_LEVEL_SCENE.resource_path, client1)
#
	#var client0_name := harness.client_player_name(client0)
	#var client0_player := await harness.wait_for_client_player_spawn(
		#client0,
		#&"TestLevel",
		#client0_name,
	#) as Node2D
	#_set_player_database(client0_player)
#
	#var client0_tp: TPComponent = client0_player.get_node("%TPComponent")
	#await _await_tp(client0_tp.teleport(
		#_tp_target(TEST_LEVEL_2_SCENE.resource_path, "TPTarget")
	#))
#
	#client0_player = await harness.wait_for_client_player_spawn(
		#client0,
		#&"TestLevel2",
		#client0_name,
	#) as Node2D
	#client0_tp = client0_player.get_node("%TPComponent")
	#await _await_tp(client0_tp.teleport(
		#_tp_target(TEST_LEVEL_SCENE.resource_path, "TestPlayerFull")
	#))
#
	#client0_player = await harness.wait_for_client_player_spawn(
		#client0,
		#&"TestLevel",
		#client0_name,
	#) as Node2D
	#assert_that(client0_player.global_position).is_equal(Vector2.ZERO)
