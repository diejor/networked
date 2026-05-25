## Integration tests for [method MultiplayerTree.connect_player].
class_name TestMultiplayerTreeConnect
extends NetwTestSuite

const TEST_LEVEL_SCENE := preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)

## Path from the level root to the [SpawnerComponent] spawn template.
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness


func before_test() -> void:
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(TEST_LEVEL_SCENE)


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	super.after_test()


func test_client_is_online_after_connect_player() -> void:
	var tree := await harness.add_connect_player(
		harness.make_join_payload("alice")
	)

	assert_that(tree.is_online()).is_true()


func test_host_player_starts_server_and_joins() -> void:
	var tree := await harness.add_host_player(
		harness.make_join_payload("alice")
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
		false
	)
	assert_that(services.size()).is_equal(1)
	assert_that(server_tree.get_service(InterestService)).is_equal(services[0])


func test_listen_server_connect_player_spawns_player() -> void:
	var tree := await harness.add_listen_server(
		harness.make_join_payload(
			"alice",
			TEST_LEVEL_SCENE.resource_path,
			SPAWNER_PATH
		)
	)

	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)

	var player := await harness.wait_for_player(tree, &"TestLevel")
	assert_that(player).is_not_null()
	assert_that(player.name).is_equal("alice|1")
