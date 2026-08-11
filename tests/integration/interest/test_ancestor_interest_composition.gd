## Integration tests for ancestry-derived scene membership and parent clamps.
class_name TestAncestorInterestComposition
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var server_scene: NetwSceneHandle
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
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)
	server_api = harness.server().api

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	assert_that(server_api.scene_instances().size()).is_equal(1)
	server_scene = server_api.scene_instances()[0]


func test_scene_wrapper_is_an_ordinary_layer_member() -> void:
	var scene_entity := server_scene.record

	assert_that(scene_entity).is_not_null()
	assert_bool(server_scene.layer.has_entity(scene_entity)).is_true()


func test_parent_row_blocks_child_layer_until_scene_admits() -> void:
	var peer_a := client0.multiplayer_peer.get_unique_id()
	var peer_b := client1.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_a)
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
	var service := harness.server().api._interest
	var own_layer := service.layer(&"always")
	assert_bool(server_scene.layer.has_entity(entity)).is_true()
	own_layer.add_entity(entity)
	own_layer.add_viewer(peer_b)
	service.flush()

	assert_bool(service.participant_sees(peer_b, entity)).is_false()
	var client1_scene := client1.api.scene(
		level_builder.scene_name,
	)
	assert_that(_find_player(client1_scene, StringName(client_player.name))) \
			.is_null()

	server_scene.admit(peer_b)
	service.flush()
	await drain_frames(get_tree(), 5)

	assert_bool(service.participant_sees(peer_b, entity)).is_true()
	var visible_player := await harness.wait_for_player(
		client1,
		level_builder.scene_name,
		StringName(client_player.name),
	)
	assert_that(visible_player).is_not_null()


func _find_player(scene: NetwSceneHandle, player_name: StringName) -> Node:
	if scene == null:
		return null
	for player: NetwEntity in scene.entities:
		var owner := player.owner
		if owner and owner.name == player_name:
			return owner
	return null
