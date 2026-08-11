## Integration tests for scene-layer admission and client projection.
##
## Covers default deny, post-admission wrapper and child membership, and state
## stream gating when an entity moves from its scene layer to another layer.
@tool
class_name TestSceneInterestMembership
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var server_scene: NetwSceneHandle
var client0: MultiplayerTree
var player_builder: PlayerBuilder
var player_with_state_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	player_with_state_builder = PlayerBuilder.new("TestPlayerWithState") \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_state([&"position"])
	player_with_state_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner(
				"..",
				[player_builder.packed, player_with_state_builder.packed],
			)
	level_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	harness.register_spawnable_scene(level_builder.packed)
	server_api = harness.server().api

	client0 = await harness.add_client()
	await harness.add_clock()

	assert_that(server_api.scene_instances().size()).is_equal(1)
	server_scene = server_api.scene_instances()[0]



func test_admission_populates_client_layer_projection() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_id)
	assert_bool(server_scene.peers.has(peer_id)).is_true()
	assert_bool(server_scene.admits(peer_id)).is_true()

	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)

	assert_bool(server_scene.layer.entities.is_empty()).is_false()
	# The layer id is derived on the server: a scene's RID is per-peer, but the
	# id it resolves to is the string both sides key the layer by.
	var layer_id := harness.server().api._scene_layer_id(server_scene.entity)
	var client_layer := client0.api._interest.layer(layer_id)
	assert_bool(client_layer.entities.is_empty()).is_false()


func test_state_stream_hidden_to_non_admitted_peers() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_id)
	harness.spawn_player(client0, player_with_state_builder.packed)
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D
	assert_that(client_player).is_not_null()

	var server_player := server_scene.level.get_node(
		NodePath(client_player.name),
	) as Node2D
	assert_that(NetwEntity.of(server_player).state_binding).is_not_null()

	var entity := NetwEntity.of(server_player)
	var service := harness.server().api._interest
	var secret_layer := service.layer(&"secret_layer")

	server_player.position = Vector2(100.0, 200.0)
	for _i in 10:
		await get_tree().process_frame
	assert_vector(client_player.position).is_equal(Vector2(100.0, 200.0))

	server_scene.layer.remove_entity(entity)
	secret_layer.add_entity(entity)
	service.flush()

	server_player.position = Vector2(300.0, 400.0)
	for _i in 10:
		await get_tree().process_frame
	assert_bool(is_instance_valid(client_player)).is_false()

	secret_layer.add_viewer(peer_id)
	service.flush()

	var revived := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	) as Node2D
	assert_that(revived).is_not_null()
	for _i in 10:
		await get_tree().process_frame
	assert_vector(revived.position).is_equal(Vector2(300.0, 400.0))

	server_player.position = Vector2(500.0, 600.0)
	for _i in 10:
		await get_tree().process_frame
	assert_vector(revived.position).is_equal(Vector2(500.0, 600.0))
