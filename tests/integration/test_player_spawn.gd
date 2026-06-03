## Tests player spawning and lifecycle with real multiplayer peers.
##
## Uses [NetwTestHarness] with a scene manager and test level scene.
## Players are spawned via [method NetwTestHarness.spawn_player] which
## bypasses the RPC chain and directly calls
## [method MultiplayerScene.add_player], testing the server-side spawn path.
class_name TestPlayerSpawn
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("TestPlayerMinimal") \
			.with_root(Node2D) \
			.with_spawner()
	player_builder.pack()

	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.add_spawnable_scene(level_builder.resource_path)
		return sm
	await harness.setup(sm_factory)
	client0 = await harness.add_client()
	client1 = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await super.after_test()


func test_spawned_player_is_in_scene() -> void:
	var player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var scene := harness.scene_on_server()
	assert_that(player.get_parent()).is_equal(scene.level)


func test_spawned_player_has_correct_authority() -> void:
	var player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var expected_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)


func test_spawned_player_has_username() -> void:
	var player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var client_comp := SpawnerComponent.unwrap(player)
	assert_that(client_comp.entity_id).is_equal("test_player_0")


func test_spawned_player_name_format() -> void:
	var player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.name).is_equal("test_player_0|%d" % peer_id)


func test_connect_peer_called_on_spawn() -> void:
	var _player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var scene := harness.scene_on_server()
	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(scene.connected_peers.has(peer_id)).is_true()


func test_two_players_in_same_scene() -> void:
	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	harness.spawn_player(client1, player_builder.packed)
	await harness.wait_for_player(client1, level_builder.scene_name)
	var scene := harness.scene_on_server()
	var peer_id_0 := client0.multiplayer_peer.get_unique_id()
	var peer_id_1 := client1.multiplayer_peer.get_unique_id()
	assert_that(scene.connected_peers.has(peer_id_0)).is_true()
	assert_that(scene.connected_peers.has(peer_id_1)).is_true()


func test_clients_admit_each_other_replicas() -> void:
	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	harness.spawn_player(client1, player_builder.packed)
	var name0 := harness.player_name_for(client0)
	var name1 := harness.player_name_for(client1)
	var client0_player1 := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
		name1,
	)
	var client1_player0 := await harness.wait_for_player(
		client1,
		level_builder.scene_name,
		name0,
	)
	var peer_id_0 := client0.multiplayer_peer.get_unique_id()
	var peer_id_1 := client1.multiplayer_peer.get_unique_id()
	var service0 := client0.get_service(InterestService) as InterestService
	var service1 := client1.get_service(InterestService) as InterestService
	assert_that(
		service0.can_peer_see_entity(
			peer_id_0,
			NetwEntity.of(client0_player1),
		),
	).is_true()
	assert_that(
		service1.can_peer_see_entity(
			peer_id_1,
			NetwEntity.of(client1_player0),
		),
	).is_true()
