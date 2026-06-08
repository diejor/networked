## End-to-end test for the user-facing player-join boundary.
##
## Drives the full [code]request_join_player[/code] RPC chain through
## [NetwTestHarness] so the assertions cover "where does this player
## actually spawn?" rather than the intermediate serde of [JoinPayload].
class_name TestJoinPlayerEndToEnd
extends NetwTestSuite

const _SPAWNER_NODE_PATH := "TestPlayerFull/MultiplayerEntity"

var harness: NetwTestHarness
var valeria: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("TestPlayerFull") \
			.with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	valeria = await harness.add_client()


func test_join_player_spawns_with_authority_and_replicates_to_client() -> void:
	var player := await harness.join_player(
		valeria,
		level_builder.resource_path,
		_SPAWNER_NODE_PATH,
	)
	assert_that(player).is_not_null()

	var scene := harness.scene_on_server(level_builder.scene_name)
	assert_that(scene).is_not_null()
	assert_that(player.get_parent()).is_equal(scene.level)
	var expected_id := valeria.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)

	var client_player := await harness.wait_for_player(valeria, level_builder.scene_name)
	assert_that(client_player).is_not_null()
