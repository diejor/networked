## Integration tests for [method MultiplayerTree.auto_connect_player] and
## [method MultiplayerTree.host_player].
class_name TestMultiplayerTreeConnect
extends NetwTestSuite

## Path from the level root to the [MultiplayerEntity] spawn template.
const SPAWNER_PATH := "TestPlayerFull/MultiplayerEntity"

var harness: NetwTestHarness
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


func test_client_is_online_after_auto_connect_player() -> void:
	var tree := await harness.add_connect_player(
		harness.make_sceneless_payload("alice"),
	)

	assert_that(tree.is_online()).is_true()


func test_host_player_starts_server_and_joins() -> void:
	var tree := await harness.add_host_player(
		harness.make_sceneless_payload("alice"),
	)

	assert_that(tree.is_online()).is_true()

	var server_node := harness.get_node_or_null("Server")
	assert_that(server_node).is_not_null()
	assert_that(server_node).is_instanceof(MultiplayerTree)
	var server_tree := server_node as MultiplayerTree

	var services := server_tree.find_children(
		"*",
		"InterestService",
		true,
		false,
	)
	assert_that(services.size()).is_equal(1)
	assert_that(server_tree.get_service(InterestService)).is_equal(services[0])


func test_listen_server_auto_connect_player_spawns_player() -> void:
	var tree := await harness.add_listen_server(
		harness.make_spawn_payload(
			"alice",
			level_builder.resource_path,
			SPAWNER_PATH,
		),
	)

	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)

	var player := await harness.wait_for_player(tree, level_builder.scene_name)
	assert_that(player).is_not_null()
	assert_that(player.name).is_equal("alice|1")
