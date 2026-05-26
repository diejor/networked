## End-to-end test for the user-facing player-join boundary.
##
## Drives the full [code]request_join_player[/code] RPC chain through
## [NetwTestHarness] so the assertions cover "where does this player
## actually spawn?" rather than the intermediate serde of [JoinPayload].
class_name TestJoinPlayerEndToEnd
extends NetwTestSuite

const _LEVEL: PackedScene = preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)
const _SPAWNER_NODE_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness
var alice: MultiplayerTree


func before_test() -> void:
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(_LEVEL)
	alice = await harness.add_client()


func after_test() -> void:
	await harness.teardown()


func test_join_player_spawns_into_named_scene() -> void:
	var player := await harness.join_player(
		alice, _LEVEL.resource_path, _SPAWNER_NODE_PATH
	)
	assert_that(player).is_not_null()

	var scene := harness.scene_on_server(&"TestLevel")
	assert_that(scene).is_not_null()
	assert_that(player.get_parent()).is_equal(scene.level)


func test_join_player_assigns_authority_to_client_peer() -> void:
	var player := await harness.join_player(
		alice, _LEVEL.resource_path, _SPAWNER_NODE_PATH
	)
	var expected_id := alice.multiplayer_peer.get_unique_id()
	assert_that(player.get_multiplayer_authority()).is_equal(expected_id)


func test_joined_player_appears_on_client() -> void:
	await harness.join_player(alice, _LEVEL.resource_path, _SPAWNER_NODE_PATH)
	var client_player := await harness.wait_for_player(alice, &"TestLevel")
	assert_that(client_player).is_not_null()
