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


func test_admission_mints_the_scene_boundary_handle() -> void:
	# Admission opens the boundary without ever asking for a handle to it, so
	# the first read is what mints one, and every later read answers the same
	# RID rather than a second handle onto one boundary.
	var peer_id := client0.multiplayer_peer.get_unique_id()
	var scene := server_scene.entity
	var layer_id := server_api._scene_layer_id(scene)

	server_scene.admit(peer_id)
	assert_bool(server_api.layer_find(layer_id).is_valid()).is_false()

	var minted := server_api.scene_get_layer(scene)
	assert_bool(minted.is_valid()).is_true()
	assert_that(server_api.layer_find(layer_id)).is_equal(minted)
	assert_that(server_api.scene_get_layer(scene)).is_equal(minted)


func test_client_holds_the_seat_for_the_scene_it_was_admitted_to() -> void:
	# A client computes no admission of its own, so its seat is something it is
	# told. This says only that it ends the pump holding one, not which of the
	# two paths that write it got there first.
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_id)
	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)

	var participant := client0.api.local_participant
	assert_object(participant).is_not_null()
	assert_object(participant.current_scene).is_not_null()
	assert_that(participant.current_scene.label) \
			.is_equal(server_scene.label)


func test_awareness_alone_writes_the_seat_the_client_was_told() -> void:
	# The seat has two writers on a client: the replicated participant record,
	# and this inference from what the interest layer made the client aware of.
	# Emptying the seat and inferring in the SAME frame leaves replication no
	# window to arrive in, so what comes back came from awareness.
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_id)
	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)

	var participant := client0.api.local_participant
	participant.current_scene = null
	assert_object(participant.current_scene).is_null()

	client0.api._scenes.sync_local_participant()

	assert_object(participant.current_scene).is_not_null()
	assert_that(participant.current_scene.label) \
			.is_equal(server_scene.label)


func test_awareness_infers_no_seat_for_a_scene_it_holds_no_entity_of() -> void:
	# The client presents the scene node either way, so presenting it is not
	# what the inference reads. Dropping the client out of the scene's layer
	# leaves the node in place and the awareness gone, and the seat stays
	# empty rather than falling back to whatever is on screen.
	var peer_id := client0.multiplayer_peer.get_unique_id()

	server_scene.admit(peer_id)
	harness.spawn_player(client0, player_builder.packed)
	await harness.wait_for_player(client0, level_builder.scene_name)

	var client_scenes := client0.api._scenes
	var layer_id := server_api._scene_layer_id(server_scene.entity)
	var client_layer := client0.api._interest.get_layer(layer_id)
	assert_bool(client_scenes.scenes.is_empty()).is_false()
	for scene_node: Node in client_scenes.scenes.values():
		client_layer._client_untrack_entity(NetwEntity.of(scene_node))
		assert_bool(client_layer.has_entity(NetwEntity.of(scene_node))) \
				.is_false()

	var participant := client0.api.local_participant
	participant.current_scene = null
	client_scenes.sync_local_participant()

	assert_object(participant.current_scene).is_null()


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
