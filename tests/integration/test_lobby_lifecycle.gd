## Integration tests for MultiplayerLobbyManager's lobby lifecycle API.
##
## Covers LoadMode startup behaviour, the full public API (preload_lobby,
## spawn_lobby, activate_lobby, freeze_lobby, destroy_lobby), and the
## automatic EmptyAction logic triggered when the last player leaves.
class_name TestLobbyLifecycle
extends NetworkedTestSuite

const LOBBY_MANAGER_SCENE := preload("res://addons/networked/core/lobby/LobbyManager.tscn")
const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_LEVEL_2_SCENE := preload("res://tests/helpers/TestLevel2.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerLobbyManager


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(LOBBY_MANAGER_SCENE)
	server_mgr = harness._get_lobby_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	server_mgr.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)
	await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		harness.teardown()
	await drain_frames(get_tree(), 3)


# --- LoadMode.ON_STARTUP ---

func test_on_startup_lobbies_spawned_after_host() -> void:
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()
	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_true()


func test_on_demand_lobby_skipped_at_startup() -> void:
	# Spin up a fresh harness with TestLevel2 configured as ON_DEMAND before host.
	var h2: NetworkTestHarness = auto_free(NetworkTestHarness.new())
	add_child(h2)
	await h2.setup(LOBBY_MANAGER_SCENE)

	var mgr2 := h2._get_lobby_manager(h2.get_server())
	mgr2._lobby_configs[&"TestLevel2"] = {
		"load_mode": MultiplayerLobbyManager.LoadMode.ON_DEMAND,
		"empty_action": MultiplayerLobbyManager.EmptyAction.FREEZE,
	}
	mgr2.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)
	mgr2.add_spawnable_scene(TEST_LEVEL_2_SCENE.resource_path)
	await h2.add_client()

	assert_that(mgr2.active_lobbies.has(&"TestLevel")).is_true()
	assert_that(mgr2.active_lobbies.has(&"TestLevel2")).is_false()


# --- preload_lobby ---

func test_preload_lobby_populates_cache() -> void:
	server_mgr.destroy_lobby(&"TestLevel2")
	await get_tree().process_frame

	var path := TEST_LEVEL_2_SCENE.resource_path
	server_mgr.preload_lobby(&"TestLevel2")

	assert_that(server_mgr._lobby_cache.has(path)).is_true()


func test_preload_lobby_does_not_instantiate() -> void:
	server_mgr.destroy_lobby(&"TestLevel2")
	await get_tree().process_frame

	server_mgr.preload_lobby(&"TestLevel2")

	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_false()


func test_spawn_after_preload_consumes_cache() -> void:
	server_mgr.destroy_lobby(&"TestLevel2")
	await get_tree().process_frame

	var path := TEST_LEVEL_2_SCENE.resource_path
	server_mgr.preload_lobby(&"TestLevel2")
	server_mgr.spawn_lobby(&"TestLevel2")

	assert_that(server_mgr._lobby_cache.has(path)).is_false()
	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_true()


# --- spawn_lobby ---

func test_spawn_lobby_adds_to_active_lobbies() -> void:
	server_mgr.destroy_lobby(&"TestLevel2")
	await get_tree().process_frame

	server_mgr.spawn_lobby(&"TestLevel2")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_true()


func test_spawn_lobby_is_idempotent() -> void:
	server_mgr.spawn_lobby(&"TestLevel")
	assert_that(server_mgr.active_lobbies.size()).is_equal(2)


# --- activate_lobby ---

func test_activate_lobby_spawns_missing_lobby() -> void:
	server_mgr.destroy_lobby(&"TestLevel2")
	await get_tree().process_frame

	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel2")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel2")).is_true()


func test_activate_lobby_sets_level_process_mode_to_inherit() -> void:
	var lobby := server_mgr.active_lobbies[&"TestLevel"]
	lobby.level.process_mode = Node.PROCESS_MODE_DISABLED

	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


# --- freeze_lobby ---

func test_freeze_lobby_sets_level_process_mode_disabled() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	server_mgr.freeze_lobby(&"TestLevel")

	var lobby := server_mgr.active_lobbies[&"TestLevel"]
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_activate_after_freeze_restores_processing() -> void:
	server_mgr.freeze_lobby(&"TestLevel")
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")

	var lobby := server_mgr.active_lobbies[&"TestLevel"]
	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_freeze_lobby_keeps_entry_in_active_lobbies() -> void:
	server_mgr.freeze_lobby(&"TestLevel")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()


# --- destroy_lobby ---

func test_destroy_lobby_removes_from_active_lobbies() -> void:
	server_mgr.destroy_lobby(&"TestLevel")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_false()


func test_destroy_lobby_frees_the_lobby_node() -> void:
	var lobby := server_mgr.active_lobbies[&"TestLevel"]
	server_mgr.destroy_lobby(&"TestLevel")
	await get_tree().process_frame
	assert_that(is_instance_valid(lobby)).is_false()


func test_destroy_then_spawn_recreates_lobby() -> void:
	server_mgr.destroy_lobby(&"TestLevel")
	await get_tree().process_frame

	server_mgr.spawn_lobby(&"TestLevel")
	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_true()


# --- EmptyAction auto-trigger ---
# These tests emit the LobbySynchronizer.despawned signal directly so they do
# not require a real multiplayer player. The connected_clients dict is empty by
# default (no players have joined in before_test), so every _apply call fires.

func test_freeze_empty_action_disables_level_on_despawn() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	var lobby := server_mgr.active_lobbies[&"TestLevel"]

	var dummy := Node.new()
	lobby.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)


func test_destroy_empty_action_removes_lobby_on_despawn() -> void:
	server_mgr._set(&"lobby_config/TestLevel/empty_action",
		MultiplayerLobbyManager.EmptyAction.DESTROY)
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	var lobby := server_mgr.active_lobbies[&"TestLevel"]

	var dummy := Node.new()
	lobby.synchronizer.despawned.emit(dummy)
	dummy.free()
	await get_tree().process_frame

	assert_that(server_mgr.active_lobbies.has(&"TestLevel")).is_false()
	assert_that(is_instance_valid(lobby)).is_false()


func test_keep_active_empty_action_leaves_level_processing() -> void:
	server_mgr._set(&"lobby_config/TestLevel/empty_action",
		MultiplayerLobbyManager.EmptyAction.KEEP_ACTIVE)
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	var lobby := server_mgr.active_lobbies[&"TestLevel"]

	var dummy := Node.new()
	lobby.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_nonempty_lobby_not_frozen_by_empty_action() -> void:
	@warning_ignore("redundant_await")
	await server_mgr.activate_lobby(&"TestLevel")
	var lobby := server_mgr.active_lobbies[&"TestLevel"]
	# Simulate a connected client so the lobby is considered non-empty.
	lobby.synchronizer.connected_clients[999] = true

	var dummy := Node.new()
	lobby.synchronizer.despawned.emit(dummy)
	dummy.free()

	assert_that(lobby.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	lobby.synchronizer.connected_clients.erase(999)
