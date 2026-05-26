## Integration test for the default-scene join flow.
##
## Verifies that dropping a Level as a direct child of [MultiplayerTree]
## automatically routes joins and spawns players via a managed scene.
class_name TestLobbylessJoin
extends NetwTestSuite

const TEST_LEVEL_SCENE := preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)
const TEST_LEVEL_PATH := "res://addons/networked_test/fixtures/TestLevel.tscn"
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness
var client: MultiplayerTree


func before_test() -> void:
	harness = make_harness()
	await harness.setup(null, TEST_LEVEL_SCENE)
	client = await harness.add_client()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()


func test_default_scene_created_on_server() -> void:
	var server := harness.server()
	var scene := server.get_node_or_null("SceneManager/TestLevelScene")
	assert_that(scene).is_not_null()


func test_level_inside_scene_on_server() -> void:
	var server := harness.server()
	var level := server.get_node_or_null(
		"SceneManager/TestLevelScene/TestLevel"
	)
	assert_that(level).is_not_null()


func test_player_spawns_in_level_after_join() -> void:
	var server := harness.server()
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	var join_payload := harness.make_join_payload(
		username,
		TEST_LEVEL_PATH,
		SPAWNER_PATH
	)

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize()
	)

	var player_name := NetwEntity.format_name(username, peer_id)
	var level := server.get_node_or_null(
		"SceneManager/TestLevelScene/TestLevel"
	)

	await wait_until(
		func(): return level != null \
			and level.get_node_or_null(player_name) != null
	)

	assert_that(level.get_node_or_null(player_name)).is_not_null()


func test_spawned_player_has_correct_username() -> void:
	var server := harness.server()
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	var join_payload := harness.make_join_payload(
		username,
		TEST_LEVEL_PATH,
		SPAWNER_PATH
	)

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize()
	)

	var player_name := NetwEntity.format_name(username, peer_id)
	var level := server.get_node_or_null(
		"SceneManager/TestLevelScene/TestLevel"
	)
	await wait_until(
		func(): return level != null \
			and level.get_node_or_null(player_name) != null
	)

	var player := level.get_node(player_name)
	var client_comp := SpawnerComponent.unwrap(player)
	assert_that(client_comp).is_not_null()
	assert_that(str(client_comp.entity_id)).is_equal(username)


func test_scene_context_accessible_from_level_node() -> void:
	var server := harness.server()
	var level := server.get_node_or_null(
		"SceneManager/TestLevelScene/TestLevel"
	)
	assert_that(level).is_not_null()

	var ctx := Netw.ctx(level)
	assert_that(ctx).is_not_null()
	assert_that(ctx.is_valid()).is_true()
