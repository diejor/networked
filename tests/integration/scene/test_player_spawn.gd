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
			.with_multiplayer_entity()
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
	await harness.setup_factory(sm_factory)
	client0 = await harness.add_client("alice")
	client1 = await harness.add_client("bob")


func test_spawned_player_joins_scene_with_identity() -> void:
	var player := harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)
	var scene := harness.scene_on_server()
	assert_that(player.get_parent()).is_equal(scene.level)

	var expected_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)

	var client_comp := MultiplayerEntity.unwrap(player)
	assert_that(client_comp.entity_id).is_equal("alice")

	var peer_id := client0.multiplayer_peer.get_unique_id()
	assert_that(player.name).is_equal("alice|%d" % peer_id)

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


func test_nested_scene_late_path_binding_and_record_forwarding() -> void:
	var builder := PlayerBuilder.new("PlayerWithWeapon")
	builder.with_root(Node2D)
	builder.with_multiplayer_entity()

	var root := builder.build()
	var weapon := TestWeaponComponent.new()
	weapon.name = "Weapon"
	root.add_child(weapon)
	weapon.owner = root

	var path := NetwPathNamespace.next_path("player", "PlayerWithWeapon")
	var packed_scene := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed_scene)
	root.free()

	var custom_level_builder := LevelBuilder.new("CustomTestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [packed_scene])
	custom_level_builder.pack()

	var custom_harness := make_unmanaged_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.add_spawnable_scene(custom_level_builder.resource_path)
		return sm
	await custom_harness.setup_factory(sm_factory)
	var custom_client := await custom_harness.add_client("carol")

	var player := custom_harness.spawn_player(custom_client, packed_scene)
	await custom_harness.wait_for_player(custom_client, custom_level_builder.scene_name)

	var mp_entity := MultiplayerEntity.unwrap(player)
	assert_that(mp_entity).is_not_null()

	var expected_path := NodePath("Weapon:ammo")
	assert_that(mp_entity.replication_config.has_property(expected_path)).is_true()

	await custom_harness.teardown()


# Inner helper class representing a nested component that resolves late
class TestWeaponComponent extends Node:
	var ammo: int = 42


	func _notification(what: int) -> void:
		if what == NOTIFICATION_PARENTED:
			var entity := NetwEntity.resolve(self)
			if entity:
				entity.contribute_spawn_property(self, &"ammo")
