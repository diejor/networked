## Integration tests for MultiplayerLobbyManager's level_spawn_function API.
##
## Verifies that custom spawn callables are invoked with the correct data, that
## levels are accessible before tree entry, that activate_lobby routes through
## lobby_spawn_data, and that EmptyAction still fires on the custom path.
class_name TestLobbyCustomSpawn
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerLobbyManager
var client_mgr: MultiplayerLobbyManager


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)
	server_mgr = harness._get_lobby_manager(harness.get_server())
	# Add a client so the server is online; capture its manager for parity setup.
	var client := await harness.add_client()
	client_mgr = harness._get_lobby_manager(client)


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
	await drain_frames(get_tree(), 3)


# --- Helper ------------------------------------------------------------------

## Sets the same callable on both sides so MultiplayerSpawner replication works
## when spawn data is not a plain String.
func _set_spawn_fn(fn: Callable) -> void:
	server_mgr.level_spawn_function = fn
	client_mgr.level_spawn_function = fn


# --- level_spawn_function invocation -----------------------------------------

func test_level_spawn_function_called_on_spawn() -> void:
	var called := [false]
	_set_spawn_fn(func(_data: Variant) -> Node:
		called[0] = true
		return TEST_LEVEL_SCENE.instantiate()
	)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)

	assert_that(called[0]).is_true()


func test_level_spawn_function_receives_correct_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return TEST_LEVEL_SCENE.instantiate()
	)

	server_mgr.spawn({"round": 7})

	assert_that(received[0]).is_equal({"round": 7})


func test_level_not_in_tree_when_spawn_function_called() -> void:
	var in_tree_during_call := [true]
	_set_spawn_fn(func(_data: Variant) -> Node:
		var level := TEST_LEVEL_SCENE.instantiate()
		in_tree_during_call[0] = level.is_inside_tree()
		return level
	)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)

	assert_that(in_tree_during_call[0]).is_false()


# --- active_lobbies keying ---------------------------------------------------

func test_custom_spawn_lobby_enters_active_lobbies() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate()
	)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)

	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()


func test_two_custom_spawns_register_independently() -> void:
	_set_spawn_fn(func(data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate() if data == "level1" \
			else TEST_LEVEL_2_SCENE.instantiate()
	)

	server_mgr.spawn("level1")
	server_mgr.spawn("level2")

	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()
	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_true()


# --- activate_lobby with level_spawn_function --------------------------------

func test_activate_lobby_uses_lobby_spawn_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return TEST_LEVEL_SCENE.instantiate()
	)
	server_mgr.lobby_spawn_data[&"TestLevel"] = {"round": 3}

	server_mgr.activate_lobby(&"TestLevel")

	assert_that(received[0]).is_equal({"round": 3})
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()


func test_activate_lobby_falls_back_to_name_when_no_spawn_data() -> void:
	var received = [null]
	_set_spawn_fn(func(data: Variant) -> Node:
		received[0] = data
		return TEST_LEVEL_SCENE.instantiate()
	)
	# No lobby_spawn_data entry — name itself should be forwarded.

	server_mgr.activate_lobby(&"TestLevel")

	assert_that(received[0]).is_equal(&"TestLevel")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()


func test_activate_lobby_wakes_level_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate()
	)
	server_mgr.lobby_spawn_data[&"TestLevel"] = &"TestLevel"

	server_mgr.activate_lobby(&"TestLevel")

	var lobby := server_mgr.active_lobbies.get(&"TestLevel") as Lobby
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_activate_lobby_does_not_respawn_when_already_active() -> void:
	var call_count := [0]
	_set_spawn_fn(func(_data: Variant) -> Node:
		call_count[0] += 1
		return TEST_LEVEL_SCENE.instantiate()
	)
	server_mgr.lobby_spawn_data[&"TestLevel"] = &"TestLevel"

	server_mgr.activate_lobby(&"TestLevel")
	server_mgr.activate_lobby(&"TestLevel")

	assert_that(call_count[0]).is_equal(1)


# --- EmptyAction on custom spawn path ----------------------------------------

func test_freeze_empty_action_applied_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate()
	)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)
	await get_tree().process_frame

	var lobby := server_mgr.active_lobbies.get(&"TestLevel") as Lobby
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_lobby_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate()
	)
	server_mgr._set(&"lobby_config/TestLevel/empty_action",
		MultiplayerLobbyManager.EmptyAction.DESTROY)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)
	await get_tree().process_frame

	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_false()


func test_keep_active_empty_action_leaves_level_processing_after_custom_spawn() -> void:
	_set_spawn_fn(func(_data: Variant) -> Node:
		return TEST_LEVEL_SCENE.instantiate()
	)
	server_mgr._set(&"lobby_config/TestLevel/empty_action",
		MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)

	server_mgr.spawn(TEST_LEVEL_SCENE.resource_path)
	await get_tree().process_frame

	var lobby := server_mgr.active_lobbies.get(&"TestLevel") as Lobby
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)
