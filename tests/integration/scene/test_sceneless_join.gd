## Integration test for the default-scene join flow.
##
## Verifies that dropping a Level as a direct child of [MultiplayerTree]
## automatically routes joins and spawns players via a managed scene.
class_name TestLobbylessJoin
extends NetwTestSuite

var harness: NetwTestHarness
var client: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var spawner_path: String


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D).with_spawner()
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	spawner_path = "%s/SpawnerComponent" % player_builder.player_name

	harness = make_harness()
	await harness.setup(null, level_builder.packed)
	client = await harness.add_client()


func test_default_scene_wraps_level_and_context() -> void:
	var server := harness.server()
	var scene_node_name := "%sScene" % level_builder.scene_name
	var scene := server.get_node_or_null("SceneManager/" + scene_node_name)
	assert_that(scene).is_not_null()

	var level := server.get_node_or_null(
		"SceneManager/%s/%s" % [scene_node_name, level_builder.scene_name],
	)
	assert_that(level).is_not_null()

	var ctx := Netw.ctx(level)
	assert_that(ctx).is_not_null()
	assert_that(ctx.is_valid()).is_true()


func test_player_spawns_in_level_after_join() -> void:
	var server := harness.server()
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	var join_payload := harness.make_spawn_payload(
		username,
		level_builder.resource_path,
		spawner_path,
	)

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize(),
	)

	var player_name := NetwEntity.format_name(username, peer_id)
	var scene_node_name := "%sScene" % level_builder.scene_name
	var level := server.get_node_or_null(
		"SceneManager/%s/%s" % [scene_node_name, level_builder.scene_name],
	)

	@warning_ignore("redundant_await")
	await assert_func(level, "get_node_or_null", [player_name]) \
			.wait_until(1000) \
			.is_not_null()

	var player := level.get_node(player_name)
	var client_comp := SpawnerComponent.unwrap(player)
	assert_that(client_comp).is_not_null()
	assert_that(str(client_comp.entity_id)).is_equal(username)
