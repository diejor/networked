## Integration tests for ancestor gate composition.
class_name TestAncestorGateComposition
extends NetwTestSuite

var harness: NetwTestHarness
var server_mgr: MultiplayerSceneManager
var server_scene: MultiplayerScene
var client0: MultiplayerTree
var client1: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)
	server_mgr = harness.server_scene_manager()

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	assert_that(server_mgr.active_scenes.size()).is_equal(1)
	server_scene = server_mgr.active_scenes.values()[0]


func test_scene_gate_is_provided_on_container_entity() -> void:
	var scene_entity := NetwEntity.of(server_scene)
	assert_that(scene_entity).is_not_null()
	assert_that(scene_entity.slot(NetwEntity.Slot.INTEREST_GATE)) \
			.is_equal(server_scene.gate)


func test_ancestor_gate_blocks_own_layer_until_scene_admits() -> void:
	var peer_a := client0.multiplayer_peer.get_unique_id()
	var peer_b := client1.multiplayer_peer.get_unique_id()

	server_scene.connect_peer(peer_a)
	harness.spawn_player(client0, player_builder.packed)
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D
	assert_that(client_player).is_not_null()

	var server_player := server_scene.level.get_node(
		NodePath(client_player.name),
	) as Node2D
	var entity := NetwEntity.of(server_player)
	var service := harness.server().get_service(InterestService) \
			as InterestService
	var own_layer := service.layer_for(&"always")
	own_layer.add_entity(entity)
	own_layer.add_viewer(peer_b)
	service.flush()

	assert_that(service._ancestors_admit(peer_b, entity)).is_false()
	assert_that(service.can_peer_see_entity(peer_b, entity)).is_false()
	var client1_mgr := harness.scene_manager_for(client1)
	var client1_scene: MultiplayerScene = client1_mgr.active_scenes.get(
		level_builder.scene_name,
	)
	assert_that(_find_player(client1_scene, StringName(client_player.name))) \
			.is_null()

	server_scene.connect_peer(peer_b)
	service.flush()
	await drain_frames(get_tree(), 5)

	assert_that(service._ancestors_admit(peer_b, entity)).is_true()
	assert_that(service.can_peer_see_entity(peer_b, entity)).is_true()
	var visible_player := await harness.wait_for_player(
		client1,
		level_builder.scene_name,
		StringName(client_player.name),
	)
	assert_that(visible_player).is_not_null()


func _find_player(scene: MultiplayerScene, player_name: StringName) -> Node:
	if scene == null:
		return null
	for player: Node in scene.player_nodes():
		if player.name == player_name:
			return player
	return null
