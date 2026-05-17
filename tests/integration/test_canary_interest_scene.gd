## Phase 4 side-by-side test: [InterestSynchronizer] pre-placed inside
## a real spawnable level, running alongside the production
## [SceneSynchronizer] inside [MultiplayerScene]. Validates that the
## canary survives the spawn flow, builds its replication_config in
## packed-scene placement (owner-before-_ready), spawn-syncs
## [member InterestSynchronizer.viewers] to clients, gates entity
## enrollment, and cleans up on [signal Node.tree_exiting] under a real
## multiplayer environment.
class_name TestCanaryInterestScene
extends NetworkedTestSuite

const CANARY_SCENE := preload("res://tests/helpers/CanaryInterestLevel.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager
var scene: MultiplayerScene
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)

	# Factory registers the canary scene on every manager (server + each
	# client). MultiplayerSpawner uses the spawnable_scenes list as an
	# index-based identifier, so both sides must register at construction.
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetworkedTestSuite.create_scene_manager()
		sm.add_spawnable_scene(CANARY_SCENE.resource_path)
		return sm
	await harness.setup(sm_factory)

	server_mgr = harness._get_scene_manager(harness.get_server())

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	# Auto-spawn (ON_STARTUP + FREEZE) does not push the scene to late
	# joiners. Driving a real join via `request_join_player` activates
	# the scene per-player, which is the path the production code takes.
	await harness.join_player(
			client0,
			CANARY_SCENE.resource_path,
			"TestPlayerFull/SpawnerComponent")
	await harness.join_player(
			client1,
			CANARY_SCENE.resource_path,
			"TestPlayerFull/SpawnerComponent")

	# Block until the scene mirrors on each client (production timing).
	await harness.wait_for_client_scene_spawn(client0, &"CanaryInterestLevel")
	await harness.wait_for_client_scene_spawn(client1, &"CanaryInterestLevel")

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func _canary() -> InterestSynchronizer:
	return scene.level.get_node("%InterestAnchor") as InterestSynchronizer


func _client_canary(client: MultiplayerTree) -> InterestSynchronizer:
	var client_sm := harness._get_scene_manager(client)
	var client_scene: MultiplayerScene = client_sm.active_scenes.get(
			scene.level.name)
	if client_scene == null or client_scene.level == null:
		return null
	var node := client_scene.level.get_node_or_null("InterestAnchor")
	if node == null:
		node = client_scene.level.get_node_or_null("%InterestAnchor")
	return node as InterestSynchronizer


# ---------------------------------------------------------------------------
# Server-side placement and defaults survive the spawn flow.
# ---------------------------------------------------------------------------

func test_canary_pre_placed_in_level() -> void:
	assert_that(_canary()).is_not_null()


func test_canary_default_state() -> void:
	var c := _canary()
	assert_that(c.policy).is_equal(
			InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS)
	assert_that(c.viewers).is_empty()
	assert_that(c.entities).is_empty()


func test_canary_replication_config_built() -> void:
	# Packed-scene placement sets owner before NOTIFICATION_PARENTED,
	# so [code]_build_replication_config[/code] should run on the
	# PARENTED path (not the _ready fallback).
	var c := _canary()
	assert_that(c.replication_config).is_not_null()


# ---------------------------------------------------------------------------
# Viewer state under live peers and over-the-wire replication.
# ---------------------------------------------------------------------------

func test_add_viewer_with_live_peer() -> void:
	var c := _canary()
	var id := client0.multiplayer_peer.get_unique_id()
	c.add_viewer(id)
	assert_that(c.has_viewer(id)).is_true()


func _client_has_viewer(client: MultiplayerTree, peer_id: int) -> bool:
	var cc := _client_canary(client)
	if cc == null:
		return false
	return cc.viewers.has(peer_id)


func test_viewers_replicate_to_client() -> void:
	var c := _canary()
	var id := client0.multiplayer_peer.get_unique_id()
	c.add_viewer(id)

	await wait_until(func() -> bool: return _client_has_viewer(client0, id))

	var client_canary := _client_canary(client0)
	assert_that(client_canary).is_not_null()
	assert_that(client_canary.viewers.has(id)).is_true()


func test_remove_viewer_replicates_to_client() -> void:
	var c := _canary()
	var id := client0.multiplayer_peer.get_unique_id()
	c.add_viewer(id)
	await wait_until(func() -> bool: return _client_has_viewer(client0, id))

	c.remove_viewer(id)
	await wait_until(func() -> bool:
		return not _client_has_viewer(client0, id))

	assert_that(_client_has_viewer(client0, id)).is_false()


# ---------------------------------------------------------------------------
# Entity enrollment + tree_exiting cleanup under live multiplayer.
# Closes the gap left by the unit-test workaround.
# ---------------------------------------------------------------------------

func test_entity_enrollment_records_pre_placed_player() -> void:
	var c := _canary()
	var player := scene.level.get_node("TestPlayerFull")
	var entity := NetwEntity.of(player)
	c.add_entity(entity)
	assert_that(c.has_entity(entity)).is_true()


func test_entity_auto_removed_on_tree_exit() -> void:
	var c := _canary()
	var player := scene.level.get_node("TestPlayerFull")
	var entity := NetwEntity.of(player)
	c.add_entity(entity)
	assert_that(c.has_entity(entity)).is_true()

	player.queue_free()
	await wait_until(func() -> bool: return not c.has_entity(entity))
	assert_that(c.has_entity(entity)).is_false()
