## Tests TPComponent teleportation logic with real multiplayer peers and the
## full request_join_player RPC join chain.
##
## TestLevel.tscn and TestLevel2.tscn have CamelCase filenames matching their
## root node names so SceneNodePath.get_scene_name() resolves to the active_lobbies key.
## Each level instances test_player_full.tscn as the spawner template.
class_name TestTPFlow
extends GdUnitTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

## Node path from level root to the ClientComponent spawner.
const SPAWNER_PATH := "TestPlayerFull/ClientComponent"

var harness: NetworkTestHarness


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(1, LOBBY_MANAGER_SCENE)

	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	server_mgr.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)
	await harness.connect_all()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
		await get_tree().process_frame

	SaveComponent.registered_components.clear()
	TPComponent._pending.clear()

	var dir := DirAccess.open("res://tests/tmp_saves")
	if dir:
		dir.list_dir_begin()
		var file := dir.get_next()
		while not file.is_empty():
			if not dir.current_is_dir():
				dir.remove(file)
			file = dir.get_next()
		dir.list_dir_end()


func test_two_lobbies_spawned() -> void:
	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	assert_that(server_mgr.active_lobbies.size()).is_equal(2)


func test_tp_spawn_places_in_correct_lobby() -> void:
	var player := await harness.join_player(
		0, TEST_LEVEL_SCENE.resource_path, SPAWNER_PATH)

	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	var lobby: Lobby = server_mgr.active_lobbies.get(&"TestLevel")
	assert_that(player.get_parent()).is_equal(lobby.level)


func test_reparent_moves_player_between_lobbies() -> void:
	var server_player := await harness.join_player(
		0, TEST_LEVEL_SCENE.resource_path, SPAWNER_PATH) as Node2D
	var client_player := await harness.wait_for_client_player_spawn(0, &"TestLevel") as Node2D

	var server_mgr: MultiplayerLobbyManager = harness.get_server().lobby_manager
	var lobby2: Lobby = server_mgr.active_lobbies.get(&"TestLevel2")

	var tp_target := SceneNodePath.new()
	tp_target.scene_path = TEST_LEVEL_2_SCENE.resource_path
	tp_target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await client_tp.teleport(tp_target).completed

	assert_that(server_player.get_parent()).is_equal(lobby2.level)


func test_teleported_snaps_to_marker() -> void:
	await harness.join_player(0, TEST_LEVEL_SCENE.resource_path, SPAWNER_PATH)
	var client_player := await harness.wait_for_client_player_spawn(0, &"TestLevel") as Node2D

	var tp_target := SceneNodePath.new()
	tp_target.scene_path = TEST_LEVEL_2_SCENE.resource_path
	tp_target.node_path = "TPTarget"

	var client_tp: TPComponent = client_player.get_node("%TPComponent")
	await client_tp.teleport(tp_target).completed

	var client_player2 := await harness.wait_for_client_player_spawn(0, &"TestLevel2") as Node2D
	assert_that(client_player2.global_position).is_equal(Vector2(100, 100))
